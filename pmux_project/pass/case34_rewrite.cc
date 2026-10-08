#include "case34_rewrite.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "pmux_v02_production_proof_api.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

struct BitLess {
	bool operator()(const RTLIL::SigBit &lhs, const RTLIL::SigBit &rhs) const
	{
		if ((lhs.wire == nullptr) != (rhs.wire == nullptr)) return lhs.wire == nullptr;
		if (lhs.wire == nullptr) return int(lhs.data) < int(rhs.data);
		if (lhs.wire->name != rhs.wire->name) return lhs.wire->name.str() < rhs.wire->name.str();
		return lhs.offset < rhs.offset;
	}
};

struct StructuralCellSnapshot {
	CellSnapshot cell;
	std::vector<std::pair<RTLIL::IdString, RTLIL::Const>> attributes;
};

bool same_cell_snapshot(const RTLIL::Cell *cell, const CellSnapshot &snapshot)
{
	if (cell == nullptr || cell->name.str() != snapshot.cell_id ||
			cell->type != snapshot.type ||
			cell->connections().size() != snapshot.ports.size() ||
			cell->parameters.size() != snapshot.parameters.size())
		return false;
	for (const CellPortSnapshot &port : snapshot.ports)
		if (!cell->hasPort(port.port) || cell->getPort(port.port) != port.signal)
			return false;
	for (const CellParameterSnapshot &parameter : snapshot.parameters)
		if (!cell->hasParam(parameter.parameter) ||
				cell->getParam(parameter.parameter) != parameter.value)
			return false;
	return true;
}

bool capture_snapshot(RTLIL::Module *module, const CellSnapshot &proof_snapshot,
		StructuralCellSnapshot &result)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(proof_snapshot.cell_id));
	if (!same_cell_snapshot(cell, proof_snapshot))
		return false;
	result.cell = proof_snapshot;
	for (const auto &attribute : cell->attributes)
		result.attributes.push_back(attribute);
	return true;
}

bool same_snapshot(RTLIL::Module *module, const StructuralCellSnapshot &snapshot)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell.cell_id));
	if (!same_cell_snapshot(cell, snapshot.cell) ||
			cell->attributes.size() != snapshot.attributes.size())
		return false;
	for (const auto &attribute : snapshot.attributes) {
		auto found = cell->attributes.find(attribute.first);
		if (found == cell->attributes.end() || found->second != attribute.second)
			return false;
	}
	return true;
}

bool binary(RTLIL::State value)
{
	return value == RTLIL::State::S0 || value == RTLIL::State::S1;
}

bool fully_binary_constant(const RTLIL::SigSpec &signal)
{
	for (RTLIL::SigBit bit : signal)
		if (bit.wire != nullptr || !binary(bit.data))
			return false;
	return true;
}

RTLIL::SigSpec remove_indices(const RTLIL::SigSpec &signal,
		const std::vector<int> &removed_indices)
{
	std::set<int> removed(removed_indices.begin(), removed_indices.end());
	RTLIL::SigSpec result;
	for (int bit_index = 0; bit_index < GetSize(signal); ++bit_index)
		if (!removed.count(bit_index)) result.append(signal[bit_index]);
	return result;
}

RTLIL::Const remove_indices(const RTLIL::Const &constant,
		const std::vector<int> &removed_indices)
{
	std::set<int> removed(removed_indices.begin(), removed_indices.end());
	std::vector<RTLIL::State> bits;
	for (int bit_index = 0; bit_index < GetSize(constant); ++bit_index)
		if (!removed.count(bit_index)) bits.push_back(constant[bit_index]);
	return RTLIL::Const(bits);
}

void reject(Case34RewriteResult &result, const std::string &reason)
{
	result.rejected_candidates++;
	result.reason_counts[reason]++;
}

bool authorized(const ProductionGuardedConeProof &proof)
{
	const ResetEnableHoldContract &contract = proof.reset_enable_hold_contract;
	return proof.status == V02ProofStatus::Normal && proof.proven_all_obligations &&
			proof.all_comparators_equivalent && proof.all_data_bits_proved &&
			proof.all_fanout_covered && proof.all_sequential_boundaries_proved &&
			contract.no_cross_dff_propagation && contract.reset_preserved &&
			contract.enable_preserved && contract.hold_preserved;
}

bool authorized(const ProductionConstantTableProof &proof)
{
	const ResetEnableHoldContract &contract = proof.reset_enable_hold_contract;
	return proof.status == V02ProofStatus::Normal && proof.proven_all_obligations &&
			proof.complete_binary_domain && proof.proven_all_rows &&
			proof.all_output_bits_mapped && contract.no_cross_dff_propagation &&
			contract.reset_preserved && contract.enable_preserved &&
			contract.hold_preserved;
}

struct GuardedPlan {
	RTLIL::Module *module = nullptr;
	ProductionGuardedConeProof proof;
	std::vector<StructuralCellSnapshot> snapshots;
	std::set<std::string> affected_cells;
	std::string anchor_cell;
};

bool build_guarded_plan(RTLIL::Module *module,
		const ProductionGuardedConeProof &proof, GuardedPlan &plan,
		Case34RewriteResult &result)
{
	result.guarded_candidates++;
	if (!authorized(proof)) {
		reject(result, v02_reason_code_name(proof.reason_code));
		return false;
	}
	if (proof.source_width <= 1 || proof.removed_bit_indices.empty() ||
			proof.comparator_cells.empty() || proof.folded_data_bits.empty()) {
		reject(result, "invalid_guarded_shape");
		return false;
	}

	std::set<std::tuple<std::string, std::string, int>> folded_positions;
	for (const ProductionFoldedDataBitConclusion &fold : proof.folded_data_bits) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(fold.cell_id));
		if (!fold.proved || !binary(fold.replacement) || cell == nullptr ||
				!cell->hasPort(fold.port) || fold.port_bit < 0 ||
				fold.port_bit >= GetSize(cell->getPort(fold.port)) ||
				!folded_positions.insert({fold.cell_id, fold.port.str(), fold.port_bit}).second) {
			reject(result, "partial_data_fold");
			return false;
		}
	}
	for (const ProductionPreservedDataBitConclusion &preserved : proof.preserved_variable_bits)
		if (!preserved.preserved) {
			reject(result, "preserved_bit_missing");
			return false;
		}
	for (const ComparatorProofConclusion &comparator : proof.comparator_cells) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(comparator.cell_id));
		if (!comparator.equivalent_under_guard || comparator.comparator_must_be_preserved ||
				cell == nullptr || !cell->type.in(ID($eq), ID($ne)) ||
				comparator.full_width != proof.source_width ||
				comparator.reduced_width != proof.source_width - GetSize(comparator.removed_bit_indices)) {
			reject(result, "comparator_fanout_uncovered");
			return false;
		}
	}

	plan.module = module;
	plan.proof = proof;
	for (const CellSnapshot &snapshot : proof.affected_cells) {
		StructuralCellSnapshot structural;
		if (!capture_snapshot(module, snapshot, structural)) {
			reject(result, "snapshot_mismatch");
			return false;
		}
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell_id));
		if (cell->get_bool_attribute(ID::keep)) {
			reject(result, "keep_attribute");
			return false;
		}
		plan.affected_cells.insert(snapshot.cell_id);
		plan.snapshots.push_back(std::move(structural));
	}
	for (const ProductionSequentialBitSnapshot &sink : proof.sequential_sinks) {
		plan.affected_cells.insert(sink.cell_id);
		bool captured = std::any_of(plan.snapshots.begin(), plan.snapshots.end(),
				[&](const StructuralCellSnapshot &item) { return item.cell.cell_id == sink.cell_id; });
		if (!captured) {
			StructuralCellSnapshot structural;
			if (!capture_snapshot(module, sink.snapshot, structural)) {
				reject(result, "sequential_boundary_changed");
				return false;
			}
			plan.snapshots.push_back(std::move(structural));
		}
	}
	plan.anchor_cell = proof.comparator_cells.front().cell_id;
	result.proven_candidates++;
	return true;
}

struct Cofactor {
	bool is_constant = false;
	RTLIL::State constant = RTLIL::State::Sx;
	std::vector<int> support;
	RTLIL::Const lut;
};

std::string cofactor_key(const Cofactor &cofactor)
{
	if (cofactor.is_constant)
		return cofactor.constant == RTLIL::State::S1 ? "constant:1" : "constant:0";
	std::ostringstream stream;
	stream << "support:";
	for (int bit : cofactor.support) stream << bit << ',';
	stream << ":lut:" << cofactor.lut.as_string();
	return stream.str();
}

bool same_cofactor(const Cofactor &lhs, const Cofactor &rhs)
{
	return cofactor_key(lhs) == cofactor_key(rhs);
}

Cofactor make_cofactor(const ProductionConstantTableProof &proof,
		int output_bit, int split_bit, int split_value)
{
	Cofactor result;
	for (int address_bit = 0; address_bit < proof.address_width; ++address_bit)
		if (address_bit != split_bit) result.support.push_back(address_bit);
	std::vector<RTLIL::State> values;
	const int rows = 1 << GetSize(result.support);
	for (int local_row = 0; local_row < rows; ++local_row) {
		int address = split_value << split_bit;
		for (int local_bit = 0; local_bit < GetSize(result.support); ++local_bit)
			address |= ((local_row >> local_bit) & 1) << result.support[local_bit];
		values.push_back(proof.truth_table.at(address)[output_bit]);
	}
	bool all_zero = true, all_one = true;
	for (RTLIL::State value : values) {
		all_zero &= value == RTLIL::State::S0;
		all_one &= value == RTLIL::State::S1;
	}
	if (all_zero || all_one) {
		result.is_constant = true;
		result.constant = all_one ? RTLIL::State::S1 : RTLIL::State::S0;
	} else {
		result.lut = RTLIL::Const(values);
	}
	return result;
}

struct SplitChoice { Cofactor low, high; };

int split_cost(const std::vector<std::vector<SplitChoice>> &choices,
		const std::vector<int> &splits, int *lut_count = nullptr,
		int *mux_count = nullptr)
{
	std::set<std::string> cofactor_cells;
	int muxes = 0;
	for (int output_bit = 0; output_bit < GetSize(splits); ++output_bit) {
		const SplitChoice &choice = choices.at(output_bit).at(splits[output_bit]);
		if (!choice.low.is_constant) cofactor_cells.insert(cofactor_key(choice.low));
		if (!choice.high.is_constant) cofactor_cells.insert(cofactor_key(choice.high));
		if (!same_cofactor(choice.low, choice.high)) muxes++;
	}
	if (lut_count) *lut_count = GetSize(cofactor_cells);
	if (mux_count) *mux_count = muxes;
	return GetSize(cofactor_cells) + muxes;
}

std::vector<int> choose_splits(const ProductionConstantTableProof &proof,
		std::vector<std::vector<SplitChoice>> &choices)
{
	choices.resize(proof.output_width);
	std::vector<int> splits(proof.output_width, 0);
	for (int output_bit = 0; output_bit < proof.output_width; ++output_bit) {
		int best_local = -1;
		for (int split = 0; split < proof.address_width; ++split) {
			SplitChoice choice = {make_cofactor(proof, output_bit, split, 0),
					make_cofactor(proof, output_bit, split, 1)};
			choices[output_bit].push_back(choice);
			std::set<std::string> local;
			if (!choice.low.is_constant) local.insert(cofactor_key(choice.low));
			if (!choice.high.is_constant) local.insert(cofactor_key(choice.high));
			int cost = GetSize(local) + !same_cofactor(choice.low, choice.high);
			if (best_local < 0 || cost < best_local) {
				best_local = cost;
				splits[output_bit] = split;
			}
		}
	}
	while (true) {
		const int old_cost = split_cost(choices, splits);
		int best_cost = old_cost;
		std::vector<int> best = splits;
		for (int output_bit = 0; output_bit < proof.output_width; ++output_bit)
			for (int split = 0; split < proof.address_width; ++split) {
				std::vector<int> candidate = splits;
				candidate[output_bit] = split;
				const int cost = split_cost(choices, candidate);
				if (cost < best_cost || (cost == best_cost && candidate < best)) {
					best_cost = cost;
					best = std::move(candidate);
				}
			}
		if (best_cost >= old_cost) break;
		splits = std::move(best);
	}
	return splits;
}

struct TablePlan {
	RTLIL::Module *module = nullptr;
	ProductionConstantTableProof proof;
	std::vector<StructuralCellSnapshot> snapshots;
	std::set<std::string> affected_cells;
	std::vector<std::vector<SplitChoice>> choices;
	std::vector<int> splits;
	std::string anchor_cell;
	int expected_lut_cells = 0;
	int expected_mux_cells = 0;
};

bool build_table_plan(RTLIL::Module *module,
		const ProductionConstantTableProof &proof, TablePlan &plan,
		Case34RewriteResult &result)
{
	result.table_candidates++;
	if (!authorized(proof)) {
		reject(result, v02_reason_code_name(proof.reason_code));
		return false;
	}
	if (proof.address_width < 2 || proof.address_width > 5 ||
			proof.output_width <= 0 || proof.output_width > 64 ||
			GetSize(proof.logical_root_outputs) != proof.output_width ||
			GetSize(proof.truth_table) != (1 << proof.address_width)) {
		reject(result, "unsupported_table_shape");
		return false;
	}
	plan.module = module;
	plan.proof = proof;
	for (const CellSnapshot &snapshot : proof.source_cells) {
		StructuralCellSnapshot structural;
		if (!capture_snapshot(module, snapshot, structural)) {
			reject(result, "snapshot_mismatch");
			return false;
		}
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell_id));
		if (cell->type != ID($mux)) {
			reject(result, "unsupported_table_source_cell");
			return false;
		}
		if (cell->get_bool_attribute(ID::keep)) {
			reject(result, "keep_attribute");
			return false;
		}
		plan.affected_cells.insert(snapshot.cell_id);
		plan.snapshots.push_back(std::move(structural));
	}
	for (const ProductionSequentialBitSnapshot &sink : proof.sequential_sinks) {
		plan.affected_cells.insert(sink.cell_id);
		bool captured = std::any_of(plan.snapshots.begin(), plan.snapshots.end(),
				[&](const StructuralCellSnapshot &item) { return item.cell.cell_id == sink.cell_id; });
		if (!captured) {
			StructuralCellSnapshot structural;
			if (!capture_snapshot(module, sink.snapshot, structural)) {
				reject(result, "sequential_boundary_changed");
				return false;
			}
			plan.snapshots.push_back(std::move(structural));
		}
	}
	if (proof.source_cells.empty()) {
		reject(result, "table_extract_failed");
		return false;
	}
	plan.anchor_cell = proof.source_cells.front().cell_id;
	plan.splits = choose_splits(plan.proof, plan.choices);
	split_cost(plan.choices, plan.splits, &plan.expected_lut_cells,
			&plan.expected_mux_cells);
	result.proven_candidates++;
	return true;
}

bool precheck(RTLIL::Module *module,
		const std::vector<StructuralCellSnapshot> &snapshots)
{
	for (const StructuralCellSnapshot &snapshot : snapshots)
		if (!same_snapshot(module, snapshot)) return false;
	return true;
}

int apply_guarded_plan(GuardedPlan &plan)
{
	for (const ComparatorProofConclusion &conclusion : plan.proof.comparator_cells) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(conclusion.cell_id));
		log_assert(cell != nullptr && cell->type.in(ID($eq), ID($ne)));
		RTLIL::SigSpec port_a = cell->getPort(ID::A);
		RTLIL::SigSpec port_b = cell->getPort(ID::B);
		const bool a_constant = fully_binary_constant(port_a);
		const bool b_constant = fully_binary_constant(port_b);
		log_assert(a_constant != b_constant);
		RTLIL::SigSpec source = a_constant ? port_b : port_a;
		RTLIL::Const constant = (a_constant ? port_a : port_b).as_const();
		RTLIL::SigSpec reduced_source = remove_indices(source, conclusion.removed_bit_indices);
		RTLIL::Const reduced_constant = remove_indices(constant, conclusion.removed_bit_indices);
		if (a_constant) {
			cell->setPort(ID::A, reduced_constant);
			cell->setPort(ID::B, reduced_source);
		} else {
			cell->setPort(ID::A, reduced_source);
			cell->setPort(ID::B, reduced_constant);
		}
		cell->setParam(ID::A_WIDTH, GetSize(cell->getPort(ID::A)));
		cell->setParam(ID::B_WIDTH, GetSize(cell->getPort(ID::B)));
		cell->setParam(ID::Y_WIDTH, 1);
	}
	int folded = 0;
	for (const ProductionFoldedDataBitConclusion &fold : plan.proof.folded_data_bits) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(fold.cell_id));
		log_assert(cell != nullptr && cell->hasPort(fold.port));
		RTLIL::SigSpec data = cell->getPort(fold.port);
		log_assert(fold.port_bit >= 0 && fold.port_bit < GetSize(data));
		data[fold.port_bit] = fold.replacement;
		cell->setPort(fold.port, data);
		folded++;
	}
	return folded;
}

RTLIL::SigBit materialize_cofactor(TablePlan &plan, const Cofactor &cofactor,
		std::map<std::string, RTLIL::SigBit> &shared, int &added_cells)
{
	if (cofactor.is_constant) return RTLIL::SigBit(cofactor.constant);
	const std::string key = cofactor_key(cofactor);
	auto found = shared.find(key);
	if (found != shared.end()) return found->second;
	RTLIL::SigSpec inputs;
	for (int address_bit : cofactor.support)
		inputs.append(plan.proof.address_bits_lsb_first.at(address_bit));
	while (GetSize(inputs) < 4)
		inputs.append(RTLIL::State::S0);
	log_assert(GetSize(inputs) == 4 && GetSize(cofactor.lut) <= 16);
	RTLIL::Wire *wire = plan.module->addWire(NEW_ID);
	RTLIL::SigBit output(wire);
	RTLIL::Cell *cell = plan.module->addCell(NEW_ID, ID(pmux_case34_lut4));
	cell->setParam(ID(LUT), cofactor.lut);
	cell->setPort(ID::A, inputs);
	cell->setPort(ID::Y, output);
	shared[key] = output;
	added_cells++;
	return output;
}

int apply_table_plan(TablePlan &plan, int &removed_cells)
{
	SigMap sigmap(plan.module);
	NewCellTypes celltypes;
	celltypes.setup(plan.module->design);
	std::set<std::string> source_ids;
	for (const CellSnapshot &snapshot : plan.proof.source_cells)
		source_ids.insert(snapshot.cell_id);
	std::set<RTLIL::SigBit, BitLess> roots;
	for (RTLIL::SigBit bit : plan.proof.logical_root_outputs)
		roots.insert(sigmap(bit));
	std::set<RTLIL::SigBit, BitLess> needed;
	for (RTLIL::Wire *wire : plan.module->wires())
		if (wire->port_output)
			for (RTLIL::SigBit bit : RTLIL::SigSpec(wire))
				if (!roots.count(sigmap(bit))) needed.insert(sigmap(bit));
	for (RTLIL::Cell *cell : plan.module->cells()) {
		if (source_ids.count(cell->name.str())) continue;
		for (const auto &connection : cell->connections())
			if (celltypes.cell_known(cell->type) &&
					celltypes.cell_input(cell->type, connection.first))
				for (RTLIL::SigBit bit : connection.second)
					if (!roots.count(sigmap(bit))) needed.insert(sigmap(bit));
	}
	bool changed = true;
	while (changed) {
		changed = false;
		for (const CellSnapshot &snapshot : plan.proof.source_cells) {
			RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(snapshot.cell_id));
			log_assert(cell != nullptr && cell->type == ID($mux));
			RTLIL::SigSpec port_a = cell->getPort(ID::A);
			RTLIL::SigSpec port_b = cell->getPort(ID::B);
			RTLIL::SigSpec port_y = cell->getPort(ID::Y);
			for (int bit_index = 0; bit_index < GetSize(port_y); ++bit_index)
				if (needed.count(sigmap(port_y[bit_index]))) {
					changed |= needed.insert(sigmap(port_a[bit_index])).second;
					changed |= needed.insert(sigmap(port_b[bit_index])).second;
				}
		}
	}
	removed_cells = 0;
	for (const CellSnapshot &snapshot : plan.proof.source_cells) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(snapshot.cell_id));
		log_assert(cell != nullptr && cell->type == ID($mux));
		RTLIL::SigSpec old_a = cell->getPort(ID::A);
		RTLIL::SigSpec old_b = cell->getPort(ID::B);
		RTLIL::SigSpec old_y = cell->getPort(ID::Y);
		RTLIL::SigSpec keep_a, keep_b, keep_y;
		for (int bit_index = 0; bit_index < GetSize(old_y); ++bit_index)
			if (needed.count(sigmap(old_y[bit_index]))) {
				keep_a.append(old_a[bit_index]);
				keep_b.append(old_b[bit_index]);
				keep_y.append(old_y[bit_index]);
			}
		if (keep_y.empty()) {
			plan.module->remove(cell);
			removed_cells++;
		} else if (GetSize(keep_y) != GetSize(old_y)) {
			cell->setPort(ID::A, keep_a);
			cell->setPort(ID::B, keep_b);
			cell->setPort(ID::Y, keep_y);
			cell->setParam(ID::WIDTH, GetSize(keep_y));
		}
	}
	std::map<std::string, RTLIL::SigBit> shared;
	int added_cells = 0;
	for (int output_bit = 0; output_bit < plan.proof.output_width; ++output_bit) {
		const int split = plan.splits.at(output_bit);
		const SplitChoice &choice = plan.choices.at(output_bit).at(split);
		RTLIL::SigBit low = materialize_cofactor(plan, choice.low, shared, added_cells);
		RTLIL::SigBit high = materialize_cofactor(plan, choice.high, shared, added_cells);
		RTLIL::SigBit target = plan.proof.logical_root_outputs[output_bit];
		if (low == high) {
			plan.module->connect(RTLIL::SigSpec(target), RTLIL::SigSpec(low));
		} else {
			plan.module->addMux(NEW_ID, RTLIL::SigSpec(low), RTLIL::SigSpec(high),
					RTLIL::SigSpec(plan.proof.address_bits_lsb_first.at(split)),
					RTLIL::SigSpec(target));
			added_cells++;
		}
	}
	return added_cells;
}

} // namespace

Case34RewriteResult run_case34_structural_rewrite(
		const std::vector<RTLIL::Module *> &input_modules)
{
	Case34RewriteResult result;
	std::vector<RTLIL::Module *> modules = input_modules;
	std::sort(modules.begin(), modules.end(), [](RTLIL::Module *lhs, RTLIL::Module *rhs) {
		return lhs->name.str() < rhs->name.str();
	});
	std::vector<GuardedPlan> guarded_plans;
	std::vector<TablePlan> table_plans;
	for (RTLIL::Module *module : modules) {
		if (module->get_bool_attribute(ID::blackbox)) continue;
		ProductionCase34ProofRequest request;
		request.module_id = module->name.str();
		request.module = module;
		std::unique_ptr<ProductionCase34Analyzer> analyzer = make_production_case34_analyzer();
		ProductionCase34ProofBundle bundle = analyzer->analyze(request);
		for (const ProductionGuardedConeProof &proof : bundle.guarded_cones) {
			GuardedPlan plan;
			if (build_guarded_plan(module, proof, plan, result))
				guarded_plans.push_back(std::move(plan));
		}
		for (const ProductionConstantTableProof &proof : bundle.constant_tables) {
			TablePlan plan;
			if (build_table_plan(module, proof, plan, result))
				table_plans.push_back(std::move(plan));
		}
	}

	std::set<std::pair<std::string, std::string>> claimed_cells;
	auto claim = [&](RTLIL::Module *module, const std::set<std::string> &cells) {
		for (const std::string &cell_id : cells)
			if (claimed_cells.count({module->name.str(), cell_id})) return false;
		for (const std::string &cell_id : cells)
			claimed_cells.insert({module->name.str(), cell_id});
		return true;
	};

	std::vector<GuardedPlan *> selected_guarded;
	std::vector<TablePlan *> selected_tables;
	for (GuardedPlan &plan : guarded_plans) {
		if (!claim(plan.module, plan.affected_cells)) {
			reject(result, "rewrite_conflict");
			continue;
		}
		if (!precheck(plan.module, plan.snapshots)) {
			reject(result, "snapshot_mismatch");
			continue;
		}
		selected_guarded.push_back(&plan);
	}
	for (TablePlan &plan : table_plans) {
		if (!claim(plan.module, plan.affected_cells)) {
			reject(result, "rewrite_conflict");
			continue;
		}
		if (!precheck(plan.module, plan.snapshots)) {
			reject(result, "snapshot_mismatch");
			continue;
		}
		selected_tables.push_back(&plan);
	}

	for (GuardedPlan *plan : selected_guarded) {
		const int folded = apply_guarded_plan(*plan);
		Case34StructuralEdit edit;
		edit.module_id = plan->module->name.str();
		edit.rule = "guarded_compare_narrowing";
		edit.anchor_cell = plan->anchor_cell;
		edit.proof_reason = v02_reason_code_name(plan->proof.reason_code);
		edit.folded_bits = folded;
		result.folded_bits += folded;
		result.edits.push_back(std::move(edit));
		result.committed_candidates++;
	}
	for (TablePlan *plan : selected_tables) {
		int removed = 0;
		const int added = apply_table_plan(*plan, removed);
		Case34StructuralEdit edit;
		edit.module_id = plan->module->name.str();
		edit.rule = "constant_table_cofactor";
		edit.anchor_cell = plan->anchor_cell;
		edit.proof_reason = v02_reason_code_name(plan->proof.reason_code);
		edit.removed_cells = removed;
		edit.added_cells = added;
		edit.split_by_output_bit = plan->splits;
		result.removed_cells += removed;
		result.added_cells += added;
		result.edits.push_back(std::move(edit));
		result.committed_candidates++;
	}
	if (result.committed_candidates == 0)
		result.reason_counts["no_v02_case34_candidate"]++;
	return result;
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
