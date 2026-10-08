#include "case34_rewrite.h"

#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "pmux_v02_case34_proof_api.h"

#include <algorithm>
#include <deque>
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
		if ((lhs.wire == nullptr) != (rhs.wire == nullptr))
			return lhs.wire == nullptr;
		if (lhs.wire == nullptr)
			return int(lhs.data) < int(rhs.data);
		if (lhs.wire->name != rhs.wire->name)
			return lhs.wire->name.str() < rhs.wire->name.str();
		return lhs.offset < rhs.offset;
	}
};

struct StructuralCellSnapshot {
	CellSnapshot cell;
	std::vector<std::pair<RTLIL::IdString, RTLIL::Const>> attributes;
};

struct Index {
	RTLIL::Module *module;
	SigMap sigmap;
	NewCellTypes celltypes;
	std::map<RTLIL::SigBit, std::vector<std::pair<RTLIL::Cell *, int>>, BitLess> drivers;
	std::map<RTLIL::SigBit, std::vector<FanoutUseSnapshot>, BitLess> uses;

	explicit Index(RTLIL::Module *module) : module(module), sigmap(module)
	{
		celltypes.setup(module->design);
		for (RTLIL::Cell *cell : module->cells()) {
			for (const auto &connection : cell->connections()) {
				const bool is_output = celltypes.cell_known(cell->type) &&
						celltypes.cell_output(cell->type, connection.first);
				const bool is_input = celltypes.cell_known(cell->type) &&
						celltypes.cell_input(cell->type, connection.first);
				for (int bit_index = 0; bit_index < GetSize(connection.second); ++bit_index) {
					RTLIL::SigBit bit = sigmap(connection.second[bit_index]);
					if (bit.wire == nullptr)
						continue;
					if (is_output)
						drivers[bit].push_back({cell, bit_index});
					if (is_input)
						uses[bit].push_back({cell->name.str(), connection.first,
								bit_index, bit});
				}
			}
		}
		for (auto &item : uses) {
			std::sort(item.second.begin(), item.second.end(),
					[](const FanoutUseSnapshot &lhs, const FanoutUseSnapshot &rhs) {
						return std::tie(lhs.cell_id, lhs.port, lhs.port_bit) <
								std::tie(rhs.cell_id, rhs.port, rhs.port_bit);
					});
		}
	}

	RTLIL::Cell *full_driver(const RTLIL::SigSpec &raw) const
	{
		RTLIL::SigSpec signal = sigmap(raw);
		RTLIL::Cell *driver = nullptr;
		for (int bit_index = 0; bit_index < GetSize(signal); ++bit_index) {
			auto found = drivers.find(signal[bit_index]);
			if (found == drivers.end() || found->second.size() != 1)
				return nullptr;
			if (driver == nullptr)
				driver = found->second.front().first;
			if (driver != found->second.front().first ||
					found->second.front().second != bit_index)
				return nullptr;
		}
		return driver;
	}

	StructuralCellSnapshot snapshot(RTLIL::Cell *cell) const
	{
		StructuralCellSnapshot result;
		result.cell.cell_id = cell->name.str();
		result.cell.type = cell->type;
		for (const auto &connection : cell->connections())
			result.cell.ports.push_back({connection.first, connection.second});
		for (const auto &parameter : cell->parameters)
			result.cell.parameters.push_back({parameter.first, parameter.second});
		for (const auto &attribute : cell->attributes)
			result.attributes.push_back(attribute);
		return result;
	}
};

bool same_snapshot(RTLIL::Module *module, const StructuralCellSnapshot &snapshot)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell.cell_id));
	if (cell == nullptr || cell->type != snapshot.cell.type)
		return false;
	if (cell->connections().size() != snapshot.cell.ports.size() ||
			cell->parameters.size() != snapshot.cell.parameters.size() ||
			cell->attributes.size() != snapshot.attributes.size())
		return false;
	for (const CellPortSnapshot &port : snapshot.cell.ports)
		if (!cell->hasPort(port.port) || cell->getPort(port.port) != port.signal)
			return false;
	for (const CellParameterSnapshot &parameter : snapshot.cell.parameters)
		if (!cell->hasParam(parameter.parameter) ||
				cell->getParam(parameter.parameter) != parameter.value)
			return false;
	for (const auto &attribute : snapshot.attributes) {
		auto found = cell->attributes.find(attribute.first);
		if (found == cell->attributes.end() || found->second != attribute.second)
			return false;
	}
	return true;
}

bool fully_binary_constant(const RTLIL::SigSpec &signal)
{
	for (RTLIL::SigBit bit : signal)
		if (bit.wire != nullptr ||
				(bit.data != RTLIL::State::S0 && bit.data != RTLIL::State::S1))
			return false;
	return true;
}

bool compare_source(Index &index, RTLIL::Cell *cell,
		RTLIL::SigSpec &source, RTLIL::Const &constant)
{
	if (cell == nullptr || !cell->type.in(ID($eq), ID($ne)))
		return false;
	RTLIL::SigSpec port_a = index.sigmap(cell->getPort(ID::A));
	RTLIL::SigSpec port_b = index.sigmap(cell->getPort(ID::B));
	if (fully_binary_constant(port_a) == fully_binary_constant(port_b))
		return false;
	RTLIL::SigSpec constant_signal = fully_binary_constant(port_a) ? port_a : port_b;
	source = fully_binary_constant(port_a) ? port_b : port_a;
	constant = constant_signal.as_const();
	return true;
}

std::vector<int> stable_unique_indices(std::vector<int> indices)
{
	std::sort(indices.begin(), indices.end());
	indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
	return indices;
}

RTLIL::SigSpec remove_indices(const RTLIL::SigSpec &signal,
		const std::vector<int> &removed_indices)
{
	std::set<int> removed(removed_indices.begin(), removed_indices.end());
	RTLIL::SigSpec result;
	for (int bit_index = 0; bit_index < GetSize(signal); ++bit_index)
		if (!removed.count(bit_index))
			result.append(signal[bit_index]);
	return result;
}

RTLIL::Const remove_indices(const RTLIL::Const &constant,
		const std::vector<int> &removed_indices)
{
	std::set<int> removed(removed_indices.begin(), removed_indices.end());
	std::vector<RTLIL::State> bits;
	for (int bit_index = 0; bit_index < GetSize(constant); ++bit_index)
		if (!removed.count(bit_index))
			bits.push_back(constant[bit_index]);
	return RTLIL::Const(bits);
}

struct GuardedPlan {
	RTLIL::Module *module = nullptr;
	RTLIL::Cell *outer = nullptr;
	RTLIL::Cell *inner = nullptr;
	RTLIL::Cell *pmux = nullptr;
	std::vector<RTLIL::Cell *> comparators;
	std::vector<StructuralCellSnapshot> snapshots;
	std::set<std::string> affected_cells;
	GuardedConeProof proof;
};

void reject(Case34RewriteResult &result, const std::string &reason)
{
	result.rejected_candidates++;
	result.reason_counts[reason]++;
}

std::vector<GuardedPlan> discover_guarded_plans(
		RTLIL::Module *module, Case34RewriteResult &result)
{
	Index index(module);
	std::unique_ptr<GuardedConeAnalyzer> analyzer = make_guarded_cone_analyzer();
	std::vector<RTLIL::Cell *> cells = module->cells();
	std::sort(cells.begin(), cells.end(), [](RTLIL::Cell *lhs, RTLIL::Cell *rhs) {
		return lhs->name.str() < rhs->name.str();
	});
	std::vector<GuardedPlan> plans;

	for (RTLIL::Cell *outer : cells) {
		if (outer->type != ID($mux) || GetSize(outer->getPort(ID::S)) != 1)
			continue;
		RTLIL::Cell *inner = index.full_driver(outer->getPort(ID::B));
		if (inner == nullptr || inner->type != ID($mux) ||
				GetSize(inner->getPort(ID::S)) != 1)
			continue;
		RTLIL::Cell *pmux = index.full_driver(inner->getPort(ID::B));
		if (pmux == nullptr || pmux->type != ID($pmux) ||
				!pmux->hasParam(ID::WIDTH) || !pmux->hasParam(ID::S_WIDTH))
			continue;

		result.guarded_candidates++;
		const int width = pmux->getParam(ID::WIDTH).as_int();
		const int branches = pmux->getParam(ID::S_WIDTH).as_int();
		if (width <= 0 || branches <= 0 ||
				GetSize(pmux->getPort(ID::B)) != width * branches) {
			reject(result, "invalid_guarded_shape");
			continue;
		}

		RTLIL::SigSpec select = pmux->getPort(ID::S);
		RTLIL::SigSpec source;
		std::vector<RTLIL::Cell *> comparators;
		std::vector<RTLIL::Const> constants;
		bool valid = true;
		for (int branch = 0; branch < branches; ++branch) {
			RTLIL::Cell *comparator = index.full_driver(RTLIL::SigSpec(select[branch]));
			RTLIL::SigSpec local_source;
			RTLIL::Const constant;
			if (!compare_source(index, comparator, local_source, constant)) {
				valid = false;
				break;
			}
			if (branch == 0)
				source = local_source;
			else if (index.sigmap(source) != index.sigmap(local_source)) {
				valid = false;
				break;
			}
			comparators.push_back(comparator);
			constants.push_back(constant);
		}
		if (!valid || source.empty()) {
			reject(result, "guarded_comparator_source_mismatch");
			continue;
		}

		RTLIL::SigBit inner_select = index.sigmap(inner->getPort(ID::S)[0]);
		std::vector<int> removed_indices;
		for (int bit_index = 0; bit_index < GetSize(source); ++bit_index)
			if (index.sigmap(source[bit_index]) == inner_select)
				removed_indices.push_back(bit_index);
		removed_indices = stable_unique_indices(removed_indices);
		if (removed_indices.empty()) {
			reject(result, "guard_not_dominating");
			continue;
		}

		GuardedConeProofRequest request;
		request.module_id = module->name.str();
		request.module = module;
		request.guard_sink_cell_id = outer->name.str();
		request.guard_sink_cell = outer;
		request.guard_facts = {
			{outer->getPort(ID::S)[0], RTLIL::State::S1},
			{inner->getPort(ID::S)[0], RTLIL::State::S1},
		};
		request.source_value = source;
		request.source_width = GetSize(source);
		request.removed_bit_indices = removed_indices;
		request.affected_cells = {
			outer->name.str(), inner->name.str(), pmux->name.str(),
		};

		for (int branch = 0; branch < branches; ++branch) {
			ComparatorNarrowingRequest comparator_request;
			comparator_request.cell_id = comparators[branch]->name.str();
			comparator_request.cell = comparators[branch];
			comparator_request.source_snapshot = source;
			comparator_request.constant_snapshot = constants[branch];
			comparator_request.removed_bit_indices = removed_indices;
			RTLIL::SigBit output = index.sigmap(comparators[branch]->getPort(ID::Y)[0]);
			comparator_request.full_fanout_snapshot = index.uses[output];
			request.comparators.push_back(comparator_request);
			request.affected_cells.push_back(comparators[branch]->name.str());
			for (const FanoutUseSnapshot &use : comparator_request.full_fanout_snapshot)
				request.affected_cells.push_back(use.cell_id);
		}

		RTLIL::SigSpec data = pmux->getPort(ID::B);
		for (int branch = 0; branch < branches; ++branch) {
			for (int bit_index = 0; bit_index < width; ++bit_index) {
				const int physical_bit = branch * width + bit_index;
				RTLIL::SigBit value = index.sigmap(data[physical_bit]);
				for (int source_bit = 0; source_bit < GetSize(source); ++source_bit) {
					if (value == index.sigmap(source[source_bit])) {
						request.folded_data_bits.push_back({physical_bit,
								data[physical_bit], constants[branch][source_bit], branch});
						break;
					}
				}
			}
		}
		for (int bit_index = 0; bit_index < width; ++bit_index)
			if (pmux->getPort(ID::A)[bit_index].wire != nullptr)
				request.preserved_variable_bits.push_back({branches * width + bit_index,
						pmux->getPort(ID::A)[bit_index]});
		if (request.folded_data_bits.empty() || request.preserved_variable_bits.empty()) {
			reject(result, "no_guarded_rewrite_opportunity");
			continue;
		}

		GuardedConeProof proof = analyzer->prove(request);
		if (proof.status != V02ProofStatus::Normal || !proof.proven_all_obligations) {
			reject(result, v02_reason_code_name(proof.reason_code));
			continue;
		}
		bool safe_to_replace = true;
		for (const ComparatorProofConclusion &conclusion : proof.comparator_cells)
			if (!conclusion.equivalent_under_guard || conclusion.comparator_must_be_preserved)
				safe_to_replace = false;
		if (!safe_to_replace) {
			reject(result, "comparator_fanout_uncovered");
			continue;
		}

		GuardedPlan plan;
		plan.module = module;
		plan.outer = outer;
		plan.inner = inner;
		plan.pmux = pmux;
		plan.comparators = comparators;
		plan.proof = std::move(proof);
		for (const std::string &cell_id : request.affected_cells)
			plan.affected_cells.insert(cell_id);
		std::set<RTLIL::Cell *> snapshot_cells = {outer, inner, pmux};
		snapshot_cells.insert(comparators.begin(), comparators.end());
		for (RTLIL::Cell *cell : snapshot_cells) {
			if (cell->get_bool_attribute(ID::keep)) {
				safe_to_replace = false;
				break;
			}
			plan.snapshots.push_back(index.snapshot(cell));
		}
		if (!safe_to_replace) {
			reject(result, "keep_attribute");
			continue;
		}
		result.proven_candidates++;
		plans.push_back(std::move(plan));
	}
	return plans;
}

bool is_sequential(RTLIL::IdString type)
{
	const std::string name = type.str();
	return name.find("dff") != std::string::npos ||
			name.find("DFF") != std::string::npos;
}

struct DiscoveredTable {
	bool ok = false;
	std::vector<RTLIL::SigBit> address_order;
	std::set<std::string> cells;
};

DiscoveredTable discover_table(Index &index, const RTLIL::SigSpec &raw,
		std::set<std::string> &visiting)
{
	RTLIL::SigSpec signal = index.sigmap(raw);
	bool is_constant = true;
	for (RTLIL::SigBit bit : signal)
		is_constant &= bit.wire == nullptr;
	if (is_constant)
		return {signal.is_fully_def(), {}, {}};

	RTLIL::Cell *cell = index.full_driver(signal);
	if (cell == nullptr || cell->type != ID($mux) ||
			GetSize(cell->getPort(ID::S)) != 1 || visiting.count(cell->name.str()))
		return {};
	visiting.insert(cell->name.str());
	DiscoveredTable port_a = discover_table(index, cell->getPort(ID::A), visiting);
	DiscoveredTable port_b = discover_table(index, cell->getPort(ID::B), visiting);
	visiting.erase(cell->name.str());
	if (!port_a.ok || !port_b.ok ||
			port_a.address_order.size() != port_b.address_order.size())
		return {};
	for (std::size_t index_bit = 0; index_bit < port_a.address_order.size(); ++index_bit)
		if (index.sigmap(port_a.address_order[index_bit]) !=
				index.sigmap(port_b.address_order[index_bit]))
			return {};

	DiscoveredTable result;
	result.ok = true;
	result.address_order = port_a.address_order;
	result.address_order.push_back(index.sigmap(cell->getPort(ID::S)[0]));
	result.cells = port_a.cells;
	result.cells.insert(port_b.cells.begin(), port_b.cells.end());
	result.cells.insert(cell->name.str());
	return result;
}

bool reaches(Index &index, RTLIL::SigBit root, RTLIL::Cell *sink)
{
	std::deque<RTLIL::SigBit> queue;
	std::set<RTLIL::SigBit, BitLess> seen;
	queue.push_back(index.sigmap(root));
	while (!queue.empty()) {
		RTLIL::SigBit bit = queue.front();
		queue.pop_front();
		if (!seen.insert(bit).second)
			continue;
		for (const FanoutUseSnapshot &use : index.uses[bit]) {
			RTLIL::Cell *cell = index.module->cell(RTLIL::IdString(use.cell_id));
			if (cell == sink && use.port == ID::D)
				return true;
			if (cell == nullptr || is_sequential(cell->type))
				continue;
			if (cell->type == ID($mux) && use.port.in(ID::A, ID::B) &&
					use.port_bit >= 0 && use.port_bit < GetSize(cell->getPort(ID::Y))) {
				queue.push_back(index.sigmap(cell->getPort(ID::Y)[use.port_bit]));
				continue;
			}
			for (const auto &connection : cell->connections())
				if (index.celltypes.cell_known(cell->type) &&
						index.celltypes.cell_output(cell->type, connection.first))
					for (RTLIL::SigBit output : connection.second)
						queue.push_back(index.sigmap(output));
		}
	}
	return false;
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
	for (int bit : cofactor.support)
		stream << bit << ',';
	stream << ":lut:" << cofactor.lut.as_string();
	return stream.str();
}

bool same_cofactor(const Cofactor &lhs, const Cofactor &rhs)
{
	return cofactor_key(lhs) == cofactor_key(rhs);
}

Cofactor make_cofactor(const ConstantTableProof &proof,
		int output_bit, int split_bit, int split_value)
{
	Cofactor result;
	for (int address_bit = 0; address_bit < proof.address_width; ++address_bit)
		if (address_bit != split_bit)
			result.support.push_back(address_bit);
	std::vector<RTLIL::State> values;
	const int rows = 1 << GetSize(result.support);
	for (int local_row = 0; local_row < rows; ++local_row) {
		int address = split_value << split_bit;
		for (int local_bit = 0; local_bit < GetSize(result.support); ++local_bit)
			address |= ((local_row >> local_bit) & 1) << result.support[local_bit];
		values.push_back(proof.truth_table.at(address)[output_bit]);
	}
	bool all_zero = true;
	bool all_one = true;
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

struct SplitChoice {
	Cofactor low;
	Cofactor high;
};

int split_cost(const std::vector<std::vector<SplitChoice>> &choices,
		const std::vector<int> &splits, int *lut_count = nullptr, int *mux_count = nullptr)
{
	std::set<std::string> cofactor_cells;
	int muxes = 0;
	for (int output_bit = 0; output_bit < GetSize(splits); ++output_bit) {
		const SplitChoice &choice = choices[output_bit][splits[output_bit]];
		if (!choice.low.is_constant)
			cofactor_cells.insert(cofactor_key(choice.low));
		if (!choice.high.is_constant)
			cofactor_cells.insert(cofactor_key(choice.high));
		if (!same_cofactor(choice.low, choice.high))
			muxes++;
	}
	if (lut_count != nullptr)
		*lut_count = GetSize(cofactor_cells);
	if (mux_count != nullptr)
		*mux_count = muxes;
	return GetSize(cofactor_cells) + muxes;
}

std::vector<int> choose_splits(const ConstantTableProof &proof,
		std::vector<std::vector<SplitChoice>> &choices)
{
	choices.resize(proof.output_width);
	std::vector<int> splits(proof.output_width, 0);
	for (int output_bit = 0; output_bit < proof.output_width; ++output_bit) {
		int best_cost = -1;
		for (int split = 0; split < proof.address_width; ++split) {
			SplitChoice choice = {
				make_cofactor(proof, output_bit, split, 0),
				make_cofactor(proof, output_bit, split, 1),
			};
			choices[output_bit].push_back(choice);
			std::set<std::string> local_cells;
			if (!choice.low.is_constant)
				local_cells.insert(cofactor_key(choice.low));
			if (!choice.high.is_constant)
				local_cells.insert(cofactor_key(choice.high));
			const int cost = GetSize(local_cells) + !same_cofactor(choice.low, choice.high);
			if (best_cost < 0 || cost < best_cost) {
				best_cost = cost;
				splits[output_bit] = split;
			}
		}
	}

	while (true) {
		const int old_cost = split_cost(choices, splits);
		int best_cost = old_cost;
		std::vector<int> best = splits;
		for (int output_bit = 0; output_bit < proof.output_width; ++output_bit) {
			for (int split = 0; split < proof.address_width; ++split) {
				std::vector<int> candidate = splits;
				candidate[output_bit] = split;
				const int cost = split_cost(choices, candidate);
				if (cost < best_cost || (cost == best_cost && cost < old_cost && candidate < best)) {
					best_cost = cost;
					best = std::move(candidate);
				}
			}
		}
		if (best_cost >= old_cost)
			break;
		splits = std::move(best);
	}
	return splits;
}

struct TablePlan {
	RTLIL::Module *module = nullptr;
	RTLIL::Cell *anchor = nullptr;
	std::vector<StructuralCellSnapshot> source_snapshots;
	std::vector<StructuralCellSnapshot> sink_snapshots;
	std::set<std::string> affected_cells;
	ConstantTableProof proof;
	std::vector<std::vector<SplitChoice>> choices;
	std::vector<int> splits;
	int expected_lut_cells = 0;
	int expected_mux_cells = 0;
};

std::vector<TablePlan> discover_table_plans(
		RTLIL::Module *module, Case34RewriteResult &result)
{
	Index index(module);
	RTLIL::SigSpec best_root;
	DiscoveredTable best;
	RTLIL::Cell *best_anchor = nullptr;
	std::vector<RTLIL::Cell *> cells = module->cells();
	std::sort(cells.begin(), cells.end(), [](RTLIL::Cell *lhs, RTLIL::Cell *rhs) {
		return lhs->name.str() < rhs->name.str();
	});
	for (RTLIL::Cell *cell : cells) {
		if (cell->type != ID($mux))
			continue;
		std::set<std::string> visiting;
		DiscoveredTable found = discover_table(index, cell->getPort(ID::Y), visiting);
		if (!found.ok)
			continue;
		if (found.address_order.size() > best.address_order.size() ||
				(found.address_order.size() == best.address_order.size() &&
				 GetSize(cell->getPort(ID::Y)) > GetSize(best_root))) {
			best = std::move(found);
			best_root = cell->getPort(ID::Y);
			best_anchor = cell;
		}
	}
	if (!best.ok || best.address_order.empty() || best_anchor == nullptr)
		return {};

	result.table_candidates++;
	ConstantTableProofRequest request;
	request.module_id = module->name.str();
	request.module = module;
	request.root_outputs = best_root;
	request.address_bits_lsb_first = best.address_order;
	for (RTLIL::SigBit bit : best_root)
		request.output_bits_lsb_first.push_back(bit);
	request.source_cells.assign(best.cells.begin(), best.cells.end());
	for (RTLIL::Cell *cell : cells) {
		if (!is_sequential(cell->type))
			continue;
		SequentialSinkRequest sink;
		sink.cell_id = cell->name.str();
		sink.cell = cell;
		sink.snapshot = index.snapshot(cell).cell;
		for (int bit_index = 0; bit_index < GetSize(best_root); ++bit_index)
			if (reaches(index, best_root[bit_index], cell))
				sink.root_output_bit_indices.push_back(bit_index);
		if (!sink.root_output_bit_indices.empty())
			request.sequential_sinks.push_back(std::move(sink));
	}

	ConstantTableProof proof = make_constant_table_analyzer()->prove(request);
	const ResetEnableHoldContract &contract = proof.reset_enable_hold_contract;
	if (proof.status != V02ProofStatus::Normal || !proof.proven_all_rows ||
			!proof.complete_binary_domain || !contract.no_cross_dff_propagation ||
			!contract.reset_preserved || !contract.enable_preserved || !contract.hold_preserved) {
		reject(result, v02_reason_code_name(proof.reason_code));
		return {};
	}
	if (proof.address_width < 2 || proof.address_width > 5 ||
			proof.output_width <= 0 || proof.output_width > 64) {
		reject(result, "unsupported_table_shape");
		return {};
	}

	TablePlan plan;
	plan.module = module;
	plan.anchor = best_anchor;
	plan.proof = std::move(proof);
	plan.splits = choose_splits(plan.proof, plan.choices);
	const int estimated = split_cost(plan.choices, plan.splits,
			&plan.expected_lut_cells, &plan.expected_mux_cells);
	if (estimated >= GetSize(plan.proof.source_cells)) {
		reject(result, "cost_not_reduced");
		return {};
	}
	for (const std::string &cell_id : plan.proof.source_cells) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(cell_id));
		if (cell == nullptr || cell->get_bool_attribute(ID::keep)) {
			reject(result, cell == nullptr ? "snapshot_mismatch" : "keep_attribute");
			return {};
		}
		plan.affected_cells.insert(cell_id);
		plan.source_snapshots.push_back(index.snapshot(cell));
	}
	for (const SequentialSinkSnapshot &sink : plan.proof.sequential_sinks) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(sink.cell_id));
		if (cell == nullptr) {
			reject(result, "snapshot_mismatch");
			return {};
		}
		plan.sink_snapshots.push_back(index.snapshot(cell));
	}
	result.proven_candidates++;
	return {std::move(plan)};
}

bool guarded_precheck(const GuardedPlan &plan)
{
	for (const StructuralCellSnapshot &snapshot : plan.snapshots)
		if (!same_snapshot(plan.module, snapshot))
			return false;
	return true;
}

bool table_precheck(const TablePlan &plan)
{
	for (const StructuralCellSnapshot &snapshot : plan.source_snapshots)
		if (!same_snapshot(plan.module, snapshot))
			return false;
	for (const StructuralCellSnapshot &snapshot : plan.sink_snapshots)
		if (!same_snapshot(plan.module, snapshot))
			return false;
	return true;
}

int apply_guarded_plan(GuardedPlan &plan)
{
	for (int comparator_index = 0;
			comparator_index < GetSize(plan.comparators); ++comparator_index) {
		RTLIL::Cell *cell = plan.comparators[comparator_index];
		const ComparatorProofConclusion &conclusion =
				plan.proof.comparator_cells.at(comparator_index);
		RTLIL::SigSpec port_a = cell->getPort(ID::A);
		RTLIL::SigSpec port_b = cell->getPort(ID::B);
		const bool a_is_constant = fully_binary_constant(port_a);
		const bool b_is_constant = fully_binary_constant(port_b);
		if (a_is_constant == b_is_constant)
			log_error("pmux_opt: guarded comparator changed after precheck.\n");
		RTLIL::SigSpec source = a_is_constant ? port_b : port_a;
		RTLIL::Const constant = (a_is_constant ? port_a : port_b).as_const();
		RTLIL::SigSpec reduced_source = remove_indices(source, conclusion.removed_bit_indices);
		RTLIL::SigSpec reduced_constant = remove_indices(constant, conclusion.removed_bit_indices);
		if (a_is_constant) {
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

	RTLIL::SigSpec data = plan.pmux->getPort(ID::B);
	int folded = 0;
	for (const GuardedDataBitConclusion &conclusion : plan.proof.folded_data_bits) {
		if (conclusion.result_bit_index < 0 ||
				conclusion.result_bit_index >= GetSize(data))
			log_error("pmux_opt: guarded data bit changed after precheck.\n");
		if (conclusion.status == GuardedDataBitStatus::FoldedZero) {
			data[conclusion.result_bit_index] = RTLIL::State::S0;
			folded++;
		} else if (conclusion.status == GuardedDataBitStatus::FoldedOne) {
			data[conclusion.result_bit_index] = RTLIL::State::S1;
			folded++;
		} else {
			log_error("pmux_opt: unproved guarded bit reached commit.\n");
		}
	}
	plan.pmux->setPort(ID::B, data);
	return folded;
}

RTLIL::SigBit materialize_cofactor(TablePlan &plan, const Cofactor &cofactor,
		std::map<std::string, RTLIL::SigBit> &shared, int &added_cells)
{
	if (cofactor.is_constant)
		return RTLIL::SigBit(cofactor.constant);
	const std::string key = cofactor_key(cofactor);
	auto found = shared.find(key);
	if (found != shared.end())
		return found->second;
	RTLIL::SigSpec inputs;
	for (int address_bit : cofactor.support)
		inputs.append(plan.proof.address_bits_lsb_first.at(address_bit));
	RTLIL::Wire *wire = plan.module->addWire(NEW_ID);
	RTLIL::SigBit output(wire);
	plan.module->addLut(NEW_ID, inputs, RTLIL::SigSpec(output), cofactor.lut);
	shared[key] = output;
	added_cells++;
	return output;
}

int apply_table_plan(TablePlan &plan)
{
	for (const std::string &cell_id : plan.proof.source_cells) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(cell_id));
		log_assert(cell != nullptr);
		plan.module->remove(cell);
	}

	std::map<std::string, RTLIL::SigBit> shared;
	int added_cells = 0;
	for (int output_bit = 0; output_bit < plan.proof.output_width; ++output_bit) {
		const int split = plan.splits.at(output_bit);
		const SplitChoice &choice = plan.choices.at(output_bit).at(split);
		RTLIL::SigBit low = materialize_cofactor(plan, choice.low, shared, added_cells);
		RTLIL::SigBit high = materialize_cofactor(plan, choice.high, shared, added_cells);
		RTLIL::SigBit target = plan.proof.root_outputs[output_bit];
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
		if (module->get_bool_attribute(ID::blackbox))
			continue;
		std::vector<GuardedPlan> guarded = discover_guarded_plans(module, result);
		for (GuardedPlan &plan : guarded)
			guarded_plans.push_back(std::move(plan));
		std::vector<TablePlan> tables = discover_table_plans(module, result);
		for (TablePlan &plan : tables)
			table_plans.push_back(std::move(plan));
	}

	std::set<std::pair<std::string, std::string>> claimed_cells;
	std::vector<GuardedPlan *> selected_guarded;
	std::vector<TablePlan *> selected_tables;
	auto claim = [&](RTLIL::Module *module, const std::set<std::string> &cells) {
		for (const std::string &cell_id : cells)
			if (claimed_cells.count({module->name.str(), cell_id}))
				return false;
		for (const std::string &cell_id : cells)
			claimed_cells.insert({module->name.str(), cell_id});
		return true;
	};

	for (GuardedPlan &plan : guarded_plans) {
		if (!claim(plan.module, plan.affected_cells)) {
			reject(result, "rewrite_conflict");
			continue;
		}
		if (!guarded_precheck(plan)) {
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
		if (!table_precheck(plan)) {
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
		edit.anchor_cell = plan->outer->name.str();
		edit.proof_reason = v02_reason_code_name(plan->proof.reason_code);
		edit.folded_bits = folded;
		result.folded_bits += folded;
		result.edits.push_back(std::move(edit));
		result.committed_candidates++;
	}
	for (TablePlan *plan : selected_tables) {
		const int removed = GetSize(plan->proof.source_cells);
		const int added = apply_table_plan(*plan);
		Case34StructuralEdit edit;
		edit.module_id = plan->module->name.str();
		edit.rule = "constant_table_cofactor";
		edit.anchor_cell = plan->anchor->name.str();
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
