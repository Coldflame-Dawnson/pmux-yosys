#include "case34_rewrite.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/celltypes.h"
#include "kernel/sigtools.h"
#include "pmux_v02_decision_cone_proof_api.h"
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

bool authorized(const DecisionConeProof &proof)
{
	const ResetEnableHoldContract &contract = proof.reset_enable_hold_contract;
	return proof.status == V02ProofStatus::Normal &&
			proof.reason_code == DecisionConeReasonCode::Proved &&
			proof.proven_all_obligations && proof.structure_complete &&
			proof.current_cycle_guard_proved && proof.selection_semantics_proved &&
			proof.guard_inside_equivalence_proved &&
			proof.guard_outside_equivalence_proved && proof.all_sink_bits_proved &&
			proof.all_fanout_covered && proof.all_sequential_boundaries_proved &&
			proof.normalized_dag_complete && contract.no_cross_dff_propagation &&
			contract.reset_preserved && contract.enable_preserved &&
			contract.hold_preserved;
}

template<typename AttributeMap>
bool same_attributes(const AttributeMap &actual,
		const std::vector<DecisionAttributeSnapshot> &expected)
{
	if (actual.size() != expected.size()) return false;
	for (const DecisionAttributeSnapshot &attribute : expected) {
		auto found = actual.find(attribute.attribute);
		if (found == actual.end() || found->second != attribute.value) return false;
	}
	return true;
}

bool same_decision_boundary(RTLIL::Module *module,
		const DecisionSequentialBitSnapshot &snapshot)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell_id));
	if (!same_cell_snapshot(cell, snapshot.cell_snapshot) ||
			!same_attributes(cell->attributes, snapshot.cell_attributes) ||
			!cell->hasPort(ID::D) || !cell->hasPort(ID::Q) ||
			snapshot.d_bit < 0 || snapshot.d_bit >= GetSize(cell->getPort(ID::D)) ||
			snapshot.q_bit < 0 || snapshot.q_bit >= GetSize(cell->getPort(ID::Q)))
		return false;
	RTLIL::SigBit q = cell->getPort(ID::Q)[snapshot.q_bit];
	return q.wire != nullptr && q.wire->port_id == snapshot.q_wire_port_id &&
			q.wire->port_input == snapshot.q_wire_port_input &&
			q.wire->port_output == snapshot.q_wire_port_output &&
			same_attributes(q.wire->attributes, snapshot.q_wire_attributes);
}

bool same_fanout_use(const FanoutUseSnapshot &lhs, const FanoutUseSnapshot &rhs)
{
	return lhs.cell_id == rhs.cell_id && lhs.port == rhs.port &&
			lhs.port_bit == rhs.port_bit && lhs.canonical_bit == rhs.canonical_bit;
}

std::vector<FanoutUseSnapshot> actual_fanout(RTLIL::Module *module,
		RTLIL::SigBit raw_bit)
{
	SigMap sigmap(module);
	NewCellTypes celltypes;
	celltypes.setup(module->design);
	const RTLIL::SigBit target = sigmap(raw_bit);
	std::vector<FanoutUseSnapshot> result;
	for (RTLIL::Cell *cell : module->cells())
		for (const auto &connection : cell->connections()) {
			bool input = celltypes.cell_known(cell->type) &&
					celltypes.cell_input(cell->type, connection.first);
			if (!celltypes.cell_known(cell->type))
				input = !connection.first.in(ID::Y, ID::Q);
			if (!input) continue;
			for (int bit = 0; bit < GetSize(connection.second); ++bit)
				if (sigmap(connection.second[bit]) == target)
					result.push_back({cell->name.str(), connection.first, bit, target});
		}
	std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
		return std::tie(lhs.cell_id, lhs.port, lhs.port_bit) <
				std::tie(rhs.cell_id, rhs.port, rhs.port_bit);
	});
	return result;
}

bool validate_comparator(RTLIL::Module *module, const DecisionConeProof &proof,
		const ComparatorProofConclusion &comparator)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(comparator.cell_id));
	if (!comparator.equivalent_under_guard || comparator.comparator_must_be_preserved ||
			comparator.reason_code != V02ReasonCode::Proved || cell == nullptr ||
			!cell->type.in(ID($eq), ID($ne)) || !cell->hasPort(ID::A) ||
			!cell->hasPort(ID::B) || !cell->hasPort(ID::Y) ||
			!cell->hasParam(ID::A_WIDTH) || !cell->hasParam(ID::B_WIDTH) ||
			!cell->hasParam(ID::Y_WIDTH) || !cell->hasParam(ID::A_SIGNED) ||
			!cell->hasParam(ID::B_SIGNED))
		return false;
	RTLIL::SigSpec port_a = cell->getPort(ID::A), port_b = cell->getPort(ID::B);
	if (GetSize(port_a) != GetSize(port_b) || GetSize(cell->getPort(ID::Y)) != 1 ||
			cell->getParam(ID::A_WIDTH).as_int() != GetSize(port_a) ||
			cell->getParam(ID::B_WIDTH).as_int() != GetSize(port_b) ||
			cell->getParam(ID::Y_WIDTH).as_int() != 1 ||
			cell->getParam(ID::A_SIGNED).as_bool() != comparator.a_signed ||
			cell->getParam(ID::B_SIGNED).as_bool() != comparator.b_signed)
		return false;
	const bool a_constant = fully_binary_constant(port_a);
	const bool b_constant = fully_binary_constant(port_b);
	if (a_constant == b_constant) return false;
	SigMap sigmap(module);
	RTLIL::SigSpec source = sigmap(a_constant ? port_b : port_a);
	RTLIL::Const constant = (a_constant ? port_a : port_b).as_const();
	if (source != sigmap(proof.source_value) || constant != comparator.comparator_constant ||
			comparator.full_width != GetSize(source) || comparator.reduced_width <= 0 ||
			comparator.reduced_width != comparator.full_width -
					GetSize(comparator.removed_bit_indices))
		return false;
	std::set<int> removed;
	for (int bit : comparator.removed_bit_indices)
		if (bit < 0 || bit >= comparator.full_width || !removed.insert(bit).second)
			return false;
	std::vector<bool> support_used(proof.support_lsb_first.size(), false);
	for (int source_bit = 0; source_bit < GetSize(source); ++source_bit) {
		if (removed.count(source_bit)) continue;
		int match = -1;
		for (int input = 0; input < GetSize(proof.support_lsb_first); ++input)
			if (source[source_bit] == sigmap(proof.support_lsb_first[input].bit)) {
				if (match >= 0) return false;
				match = input;
			}
		if (match < 0 || support_used[match]) return false;
		support_used[match] = true;
	}
	if (std::any_of(support_used.begin(), support_used.end(), [](bool used) { return !used; }))
		return false;
	auto actual = actual_fanout(module, cell->getPort(ID::Y)[0]);
	auto expected = comparator.comparator_full_fanout_snapshot;
	std::sort(expected.begin(), expected.end(), [](const auto &lhs, const auto &rhs) {
		return std::tie(lhs.cell_id, lhs.port, lhs.port_bit) <
				std::tie(rhs.cell_id, rhs.port, rhs.port_bit);
	});
	return actual.size() == expected.size() &&
			std::equal(actual.begin(), actual.end(), expected.begin(), same_fanout_use);
}

struct DecisionPlan {
	RTLIL::Module *module = nullptr;
	DecisionConeProof proof;
	std::vector<StructuralCellSnapshot> snapshots;
	std::vector<DecisionSequentialBitSnapshot> boundaries;
	std::set<std::string> affected_cells;
	std::string anchor_cell;
};

bool build_decision_plan(RTLIL::Module *module, const DecisionConeProof &proof,
		DecisionPlan &plan, Case34RewriteResult &result)
{
	if (proof.reason_code == DecisionConeReasonCode::StructureNotFound)
		return false;
	result.decision_cone_candidates++;
	if (!authorized(proof)) {
		reject(result, decision_cone_reason_code_name(proof.reason_code));
		return false;
	}
	if (proof.module != module->name.str() ||
			proof.support_lsb_first.empty() || proof.support_lsb_first.size() > 12 ||
			proof.normalized_dag.size() < 2 || proof.normalized_dag.size() > 4096 ||
			proof.affected_sinks.empty() || proof.source_cells.empty() ||
			proof.comparators.empty() || proof.decision_relations.empty() ||
			proof.proof_ids.empty()) {
		reject(result, "invalid_decision_cone_shape");
		return false;
	}
	const std::size_t truth_rows = std::size_t(1) << proof.support_lsb_first.size();
	std::set<std::string> unique_proof_ids;
	auto unique_proof_id = [&](const std::string &proof_id) {
		return !proof_id.empty() && unique_proof_ids.insert(proof_id).second;
	};
	for (const std::string &proof_id : proof.proof_ids)
		if (!unique_proof_id(proof_id)) {
			reject(result, "duplicate_proof_id");
			return false;
		}
	for (int index = 0; index < GetSize(proof.support_lsb_first); ++index)
		if (proof.support_lsb_first[index].input_index != index ||
				proof.support_lsb_first[index].bit.wire == nullptr ||
				!unique_proof_id(proof.support_lsb_first[index].proof_id)) {
			reject(result, "invalid_decision_support");
			return false;
		}
	for (int index = 0; index < GetSize(proof.normalized_dag); ++index) {
		const DecisionDagNode &node = proof.normalized_dag[index];
		if (node.node_id != index ||
				(node.kind == DecisionDagNodeKind::Test &&
				(node.input_index < 0 || node.input_index >= GetSize(proof.support_lsb_first) ||
				node.low_node < 0 || node.low_node >= index ||
				node.high_node < 0 || node.high_node >= index)) ||
				!unique_proof_id(node.proof_id)) {
			reject(result, "invalid_decision_dag");
			return false;
		}
		if (node.kind != DecisionDagNodeKind::Constant0 &&
				node.kind != DecisionDagNodeKind::Constant1 &&
				node.kind != DecisionDagNodeKind::Test) {
			reject(result, "invalid_decision_dag");
			return false;
		}
	}
	for (const ComparatorProofConclusion &comparator : proof.comparators)
		if (!validate_comparator(module, proof, comparator)) {
			reject(result, "comparator_fanout_uncovered");
			return false;
		}
	for (const DecisionRelationProof &relation : proof.decision_relations) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(relation.cell_id));
		if (relation.reason_code != DecisionConeReasonCode::Proved ||
				!relation.overlap_checked || !relation.last_assignment_semantics_proved ||
				!relation.pairwise_disjoint || relation.proof_id.empty() ||
				!same_cell_snapshot(cell, relation.snapshot)) {
			reject(result, "invalid_decision_relation");
			return false;
		}
		std::vector<RTLIL::SigBit> expected_selects;
		if (relation.kind == DecisionRelationKind::MuxPriority ||
				relation.kind == DecisionRelationKind::PmuxMutuallyExclusive) {
			if (cell == nullptr || !cell->type.in(ID($mux), ID($pmux)) || !cell->hasPort(ID::S)) {
				reject(result, "invalid_decision_relation");
				return false;
			}
			expected_selects = cell->getPort(ID::S).to_sigbit_vector();
		} else if (relation.kind == DecisionRelationKind::SequentialControlPriority) {
			if (cell == nullptr) {
				reject(result, "invalid_decision_relation");
				return false;
			}
			for (RTLIL::IdString port : {ID::ARST, ID::EN, ID::SRST})
				if (cell->hasPort(port)) expected_selects.push_back(cell->getPort(port)[0]);
		} else {
			reject(result, "invalid_decision_relation");
			return false;
		}
		if (expected_selects != relation.selects_in_branch_order) {
			reject(result, "invalid_decision_relation");
			return false;
		}
	}

	plan.module = module;
	plan.proof = proof;
	auto capture = [&](const CellSnapshot &snapshot) {
		StructuralCellSnapshot structural;
		if (!capture_snapshot(module, snapshot, structural)) return false;
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(snapshot.cell_id));
		if (cell->get_bool_attribute(ID::keep)) return false;
		plan.affected_cells.insert(snapshot.cell_id);
		plan.snapshots.push_back(std::move(structural));
		return true;
	};
	std::set<std::string> captured_cells;
	for (const CellSnapshot &snapshot : proof.source_cells)
		if (captured_cells.insert(snapshot.cell_id).second && !capture(snapshot)) {
			reject(result, "snapshot_mismatch");
			return false;
		}
	for (const CellSnapshot &snapshot : proof.external_consumers)
		if (captured_cells.insert(snapshot.cell_id).second && !capture(snapshot)) {
			reject(result, "uncovered_fanout");
			return false;
		}
	std::set<std::pair<std::string, int>> sink_positions;
	std::set<int> logical_sink_bits;
	for (const DecisionSinkProof &sink : proof.affected_sinks) {
		const DecisionSequentialBitSnapshot &boundary = sink.sequential_snapshot;
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(boundary.cell_id));
		if (sink.reason_code != DecisionConeReasonCode::Proved ||
				sink.cell_id != boundary.cell_id || sink.d_bit != boundary.d_bit ||
				sink.q_bit != boundary.q_bit ||
				sink.logical_sink_bit != boundary.logical_sink_bit ||
				sink.cell != cell || boundary.cell != cell ||
				sink.proof_id.empty() || sink.proof_id != boundary.proof_id ||
				!logical_sink_bits.insert(boundary.logical_sink_bit).second ||
				std::size_t(GetSize(sink.care_mask_lsb_address)) != truth_rows ||
				std::size_t(GetSize(sink.value_mask_lsb_address)) != truth_rows ||
				!sink.equivalent_on_all_care_rows ||
				!sink.guard_outside_behavior_preserved || !sink.default_path_preserved ||
				!sink.hold_path_preserved || !sink.reset_path_preserved ||
				!sink.enable_path_preserved || !sink.initialization_preserved ||
				!sink.no_cross_dff_propagation || !sink.all_fanout_covered ||
				sink.normalized_root_node < 0 ||
				sink.normalized_root_node >= GetSize(proof.normalized_dag) ||
				!same_decision_boundary(module, boundary) || cell == nullptr ||
				cell->getPort(ID::D)[boundary.d_bit] != sink.original_d ||
				!sink_positions.insert({boundary.cell_id, boundary.d_bit}).second) {
			reject(result, "partial_decision_sink");
			return false;
		}
		for (std::size_t row = 0; row < truth_rows; ++row)
			if (!binary(sink.care_mask_lsb_address[row]) ||
					!binary(sink.value_mask_lsb_address[row])) {
				reject(result, "invalid_decision_truth_domain");
				return false;
			}
		if (cell->get_bool_attribute(ID::keep)) {
			reject(result, "keep_attribute");
			return false;
		}
		plan.affected_cells.insert(boundary.cell_id);
		plan.boundaries.push_back(boundary);
		if (captured_cells.insert(boundary.cell_id).second && !capture(boundary.cell_snapshot)) {
			reject(result, "sequential_boundary_changed");
			return false;
		}
	}
	for (const DecisionSequentialBitSnapshot &boundary : proof.unaffected_sequential_bits) {
		if (!same_decision_boundary(module, boundary)) {
			reject(result, "sequential_boundary_changed");
			return false;
		}
		plan.affected_cells.insert(boundary.cell_id);
		plan.boundaries.push_back(boundary);
		if (captured_cells.insert(boundary.cell_id).second && !capture(boundary.cell_snapshot)) {
			reject(result, "sequential_boundary_changed");
			return false;
		}
	}
	plan.anchor_cell = proof.source_cells.front().cell_id;
	result.proven_candidates++;
	return true;
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

bool precheck(DecisionPlan &plan)
{
	if (!precheck(plan.module, plan.snapshots)) return false;
	for (const DecisionSequentialBitSnapshot &boundary : plan.boundaries)
		if (!same_decision_boundary(plan.module, boundary)) return false;
	for (const DecisionSinkProof &sink : plan.proof.affected_sinks) {
		const DecisionSequentialBitSnapshot &boundary = sink.sequential_snapshot;
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(boundary.cell_id));
		if (cell == nullptr || !cell->hasPort(ID::D) || boundary.d_bit < 0 ||
				boundary.d_bit >= GetSize(cell->getPort(ID::D)) ||
				cell->getPort(ID::D)[boundary.d_bit] != sink.original_d)
			return false;
	}
	return true;
}

void apply_comparator_narrowing(RTLIL::Module *module,
		const std::vector<ComparatorProofConclusion> &conclusions)
{
	for (const ComparatorProofConclusion &conclusion : conclusions) {
		RTLIL::Cell *cell = module->cell(RTLIL::IdString(conclusion.cell_id));
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
}

int apply_decision_plan(DecisionPlan &plan)
{
	apply_comparator_narrowing(plan.module, plan.proof.comparators);
	struct ProofAtom { RTLIL::SigBit signal; RTLIL::Const truth; };
	std::vector<ProofAtom> atoms;
	const std::size_t truth_rows = std::size_t(1) << plan.proof.support_lsb_first.size();
	for (int input = 0; input < GetSize(plan.proof.support_lsb_first); ++input) {
		std::vector<RTLIL::State> truth;
		for (std::size_t row = 0; row < truth_rows; ++row)
			truth.push_back((row >> std::size_t(input)) & 1 ? RTLIL::State::S1 : RTLIL::State::S0);
		atoms.push_back({plan.proof.support_lsb_first[input].bit, RTLIL::Const(truth)});
	}
	SigMap sigmap(plan.module);
	for (const ComparatorProofConclusion &comparator : plan.proof.comparators) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(comparator.cell_id));
		log_assert(cell != nullptr && cell->type.in(ID($eq), ID($ne)) && cell->hasPort(ID::Y));
		std::set<int> removed(comparator.removed_bit_indices.begin(),
				comparator.removed_bit_indices.end());
		std::vector<int> source_to_input(GetSize(plan.proof.source_value), -1);
		for (int source_bit = 0; source_bit < GetSize(plan.proof.source_value); ++source_bit)
			if (!removed.count(source_bit))
				for (int input = 0; input < GetSize(plan.proof.support_lsb_first); ++input)
					if (sigmap(plan.proof.source_value[source_bit]) ==
							sigmap(plan.proof.support_lsb_first[input].bit)) {
						source_to_input[source_bit] = input;
						break;
					}
		std::vector<RTLIL::State> truth;
		for (std::size_t row = 0; row < truth_rows; ++row) {
			bool equal = true;
			for (int source_bit = 0; source_bit < GetSize(plan.proof.source_value); ++source_bit)
				if (!removed.count(source_bit)) {
					log_assert(source_to_input[source_bit] >= 0 &&
							binary(comparator.comparator_constant[source_bit]));
					bool value = (row >> std::size_t(source_to_input[source_bit])) & 1;
					equal &= value == (comparator.comparator_constant[source_bit] == RTLIL::State::S1);
				}
			bool output = cell->type == ID($eq) ? equal : !equal;
			truth.push_back(output ? RTLIL::State::S1 : RTLIL::State::S0);
		}
		atoms.push_back({cell->getPort(ID::Y)[0], RTLIL::Const(truth)});
	}

	enum class SinkValueKind { Dag, Constant0, Constant1, Atom, InvertedAtom, And, Or, Xor };
	struct SinkValue { SinkValueKind kind = SinkValueKind::Dag; int index = -1; int rhs = -1; };
	std::vector<SinkValue> sink_values;
	std::vector<bool> needed(plan.proof.normalized_dag.size(), false);
	auto matches = [](const DecisionSinkProof &sink, auto value_at) {
		for (std::size_t row = 0; row < sink.care_mask_lsb_address.size(); ++row)
			if (sink.care_mask_lsb_address[row] == RTLIL::State::S1) {
				RTLIL::State expected = sink.value_mask_lsb_address[row];
				if (!binary(expected) || value_at(row) != expected) return false;
			}
		return true;
	};
	for (const DecisionSinkProof &sink : plan.proof.affected_sinks) {
		SinkValue value;
		if (matches(sink, [](int) { return RTLIL::State::S0; }))
			value.kind = SinkValueKind::Constant0;
		else if (matches(sink, [](int) { return RTLIL::State::S1; }))
			value.kind = SinkValueKind::Constant1;
		else for (int atom = 0; atom < GetSize(atoms); ++atom) {
			if (matches(sink, [&](int row) { return atoms[atom].truth[row]; })) {
				value.kind = SinkValueKind::Atom; value.index = atom; break;
			}
			if (matches(sink, [&](int row) {
				return atoms[atom].truth[row] == RTLIL::State::S1 ? RTLIL::State::S0 : RTLIL::State::S1;
			})) {
				value.kind = SinkValueKind::InvertedAtom; value.index = atom; break;
			}
		}
		if (value.kind == SinkValueKind::Dag)
			for (int lhs = 0; lhs < GetSize(atoms) && value.kind == SinkValueKind::Dag; ++lhs)
				for (int rhs = lhs + 1; rhs < GetSize(atoms); ++rhs) {
					if (matches(sink, [&](int row) {
						return atoms[lhs].truth[row] == RTLIL::State::S1 &&
								atoms[rhs].truth[row] == RTLIL::State::S1 ? RTLIL::State::S1 : RTLIL::State::S0;
					})) {
						value.kind = SinkValueKind::And; value.index = lhs; value.rhs = rhs; break;
					}
					if (matches(sink, [&](int row) {
						return atoms[lhs].truth[row] == RTLIL::State::S1 ||
								atoms[rhs].truth[row] == RTLIL::State::S1 ? RTLIL::State::S1 : RTLIL::State::S0;
					})) {
						value.kind = SinkValueKind::Or; value.index = lhs; value.rhs = rhs; break;
					}
					if (matches(sink, [&](int row) {
						return atoms[lhs].truth[row] != atoms[rhs].truth[row] ? RTLIL::State::S1 : RTLIL::State::S0;
					})) {
						value.kind = SinkValueKind::Xor; value.index = lhs; value.rhs = rhs; break;
					}
				}
		if (value.kind == SinkValueKind::Dag) {
			value.index = sink.normalized_root_node;
			needed.at(value.index) = true;
		}
		sink_values.push_back(value);
	}
	for (int node_id = GetSize(plan.proof.normalized_dag) - 1; node_id >= 0; --node_id)
		if (needed[node_id] && plan.proof.normalized_dag[node_id].kind == DecisionDagNodeKind::Test) {
			needed[plan.proof.normalized_dag[node_id].low_node] = true;
			needed[plan.proof.normalized_dag[node_id].high_node] = true;
		}
	std::vector<RTLIL::SigBit> values;
	values.reserve(plan.proof.normalized_dag.size());
	int added_cells = 0;
	std::map<RTLIL::SigBit, RTLIL::SigBit, BitLess> inverted;
	auto add_not = [&](RTLIL::SigBit input) {
		auto found = inverted.find(input);
		if (found != inverted.end()) return found->second;
		RTLIL::Wire *wire = plan.module->addWire(NEW_ID);
		RTLIL::SigBit output(wire);
		plan.module->addNot(NEW_ID, RTLIL::SigSpec(input), RTLIL::SigSpec(output));
		inverted[input] = output;
		added_cells++;
		return output;
	};
	auto add_gate = [&](RTLIL::IdString type, RTLIL::SigBit lhs, RTLIL::SigBit rhs) {
		RTLIL::Wire *wire = plan.module->addWire(NEW_ID);
		RTLIL::SigBit output(wire);
		if (type == ID($and))
			plan.module->addAnd(NEW_ID, RTLIL::SigSpec(lhs), RTLIL::SigSpec(rhs),
					RTLIL::SigSpec(output));
		else if (type == ID($or))
			plan.module->addOr(NEW_ID, RTLIL::SigSpec(lhs), RTLIL::SigSpec(rhs),
					RTLIL::SigSpec(output));
		else
			plan.module->addXor(NEW_ID, RTLIL::SigSpec(lhs), RTLIL::SigSpec(rhs),
					RTLIL::SigSpec(output));
		added_cells++;
		return output;
	};
	for (const DecisionDagNode &node : plan.proof.normalized_dag) {
		if (!needed[node.node_id]) {
			values.push_back(RTLIL::State::Sx);
			continue;
		}
		if (node.kind == DecisionDagNodeKind::Constant0) {
			values.push_back(RTLIL::State::S0);
			continue;
		}
		if (node.kind == DecisionDagNodeKind::Constant1) {
			values.push_back(RTLIL::State::S1);
			continue;
		}
		log_assert(node.kind == DecisionDagNodeKind::Test &&
				node.low_node >= 0 && node.low_node < GetSize(values) &&
				node.high_node >= 0 && node.high_node < GetSize(values));
		RTLIL::SigBit low = values[node.low_node];
		RTLIL::SigBit high = values[node.high_node];
		RTLIL::SigBit select = plan.proof.support_lsb_first[node.input_index].bit;
		RTLIL::SigBit output;
		if (low == high) {
			output = low;
		} else if (low == RTLIL::State::S0 && high == RTLIL::State::S1) {
			output = select;
		} else if (low == RTLIL::State::S1 && high == RTLIL::State::S0) {
			output = add_not(select);
		} else if (low == RTLIL::State::S0) {
			output = add_gate(ID($and), select, high);
		} else if (high == RTLIL::State::S0) {
			output = add_gate(ID($and), add_not(select), low);
		} else if (low == RTLIL::State::S1) {
			output = add_gate(ID($or), add_not(select), high);
		} else if (high == RTLIL::State::S1) {
			output = add_gate(ID($or), select, low);
		} else {
			RTLIL::Wire *wire = plan.module->addWire(NEW_ID);
			output = RTLIL::SigBit(wire);
			plan.module->addMux(NEW_ID, RTLIL::SigSpec(low), RTLIL::SigSpec(high),
					RTLIL::SigSpec(select), RTLIL::SigSpec(output));
			added_cells++;
		}
		values.push_back(output);
	}

	std::map<std::string, RTLIL::SigSpec> updated_d;
	for (int sink_index = 0; sink_index < GetSize(plan.proof.affected_sinks); ++sink_index) {
		const DecisionSinkProof &sink = plan.proof.affected_sinks[sink_index];
		const DecisionSequentialBitSnapshot &boundary = sink.sequential_snapshot;
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(boundary.cell_id));
		log_assert(cell != nullptr && cell->hasPort(ID::D));
		auto inserted = updated_d.emplace(boundary.cell_id, cell->getPort(ID::D));
		RTLIL::SigSpec &port_d = inserted.first->second;
		log_assert(boundary.d_bit >= 0 && boundary.d_bit < GetSize(port_d));
		const SinkValue &replacement = sink_values[sink_index];
		if (replacement.kind == SinkValueKind::Constant0)
			port_d[boundary.d_bit] = RTLIL::State::S0;
		else if (replacement.kind == SinkValueKind::Constant1)
			port_d[boundary.d_bit] = RTLIL::State::S1;
		else if (replacement.kind == SinkValueKind::Atom)
			port_d[boundary.d_bit] = atoms[replacement.index].signal;
		else if (replacement.kind == SinkValueKind::InvertedAtom)
			port_d[boundary.d_bit] = add_not(atoms[replacement.index].signal);
		else if (replacement.kind == SinkValueKind::And)
			port_d[boundary.d_bit] = add_gate(ID($and), atoms[replacement.index].signal,
					atoms[replacement.rhs].signal);
		else if (replacement.kind == SinkValueKind::Or)
			port_d[boundary.d_bit] = add_gate(ID($or), atoms[replacement.index].signal,
					atoms[replacement.rhs].signal);
		else if (replacement.kind == SinkValueKind::Xor)
			port_d[boundary.d_bit] = add_gate(ID($xor), atoms[replacement.index].signal,
					atoms[replacement.rhs].signal);
		else {
			log_assert(replacement.index >= 0 && replacement.index < GetSize(values));
			port_d[boundary.d_bit] = values[replacement.index];
		}
	}
	for (const auto &update : updated_d) {
		RTLIL::Cell *cell = plan.module->cell(RTLIL::IdString(update.first));
		log_assert(cell != nullptr);
		cell->setPort(ID::D, update.second);
	}
	return added_cells;
}

int apply_guarded_plan(GuardedPlan &plan)
{
	apply_comparator_narrowing(plan.module, plan.proof.comparator_cells);
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

DecisionConsumerMutationTestResult run_decision_consumer_mutation_tests(
		const std::vector<RTLIL::Module *> &modules)
{
	DecisionConsumerMutationTestResult outcome;
	auto fingerprint = [](RTLIL::Module *module) {
		std::ostringstream stream;
		RTLIL_BACKEND::dump_module(stream, "", module, module->design, false);
		return stream.str();
	};
	for (RTLIL::Module *module : modules) {
		DecisionConeProofRequest request;
		request.module_id = module->name.str();
		request.module = module;
		DecisionConeProof proof = make_decision_cone_analyzer()->analyze(request);
		DecisionPlan baseline_plan;
		Case34RewriteResult baseline_result;
		const std::string baseline_fingerprint = fingerprint(module);
		if (!build_decision_plan(module, proof, baseline_plan, baseline_result))
			continue;
		outcome.checks++;
		if (fingerprint(module) == baseline_fingerprint)
			outcome.passed++;
		else
			outcome.failures.push_back("valid_proof_validator_wrote_rtlil");

		auto reject_mutation = [&](const std::string &name, auto mutate) {
			DecisionConeProof changed = proof;
			mutate(changed);
			const std::string before = fingerprint(module);
			DecisionPlan plan;
			Case34RewriteResult result;
			const bool accepted = build_decision_plan(module, changed, plan, result);
			const bool unchanged = before == fingerprint(module);
			outcome.checks++;
			if (!accepted && unchanged)
				outcome.passed++;
			else
				outcome.failures.push_back(name + (accepted ? ":accepted" : ":rtlil_changed"));
		};
		reject_mutation("sink_cell_cross_binding", [](auto &p) { p.affected_sinks[0].cell_id += "$mutated"; });
		reject_mutation("sink_d_cross_binding", [](auto &p) { p.affected_sinks[0].d_bit++; });
		reject_mutation("sink_q_cross_binding", [](auto &p) { p.affected_sinks[0].q_bit++; });
		reject_mutation("sink_logical_cross_binding", [](auto &p) { p.affected_sinks[0].logical_sink_bit++; });
		if (proof.affected_sinks.size() > 1)
			reject_mutation("duplicate_logical_sink", [](auto &p) {
				p.affected_sinks[1].logical_sink_bit = p.affected_sinks[0].logical_sink_bit;
				p.affected_sinks[1].sequential_snapshot.logical_sink_bit = p.affected_sinks[0].logical_sink_bit;
			});
		if (proof.affected_sinks.size() > 1)
			reject_mutation("duplicate_physical_sink", [](auto &p) {
				p.affected_sinks[1].cell_id = p.affected_sinks[0].cell_id;
				p.affected_sinks[1].cell = p.affected_sinks[0].cell;
				p.affected_sinks[1].d_bit = p.affected_sinks[0].d_bit;
				p.affected_sinks[1].q_bit = p.affected_sinks[0].q_bit;
				p.affected_sinks[1].sequential_snapshot = p.affected_sinks[0].sequential_snapshot;
				p.affected_sinks[1].sequential_snapshot.logical_sink_bit = p.affected_sinks[1].logical_sink_bit;
				p.affected_sinks[1].sequential_snapshot.proof_id = p.affected_sinks[1].proof_id;
			});
		reject_mutation("comparator_constant", [](auto &p) {
			p.comparators[0].comparator_constant.bits()[0] =
					p.comparators[0].comparator_constant[0] == RTLIL::State::S1 ?
					RTLIL::State::S0 : RTLIL::State::S1;
		});
		reject_mutation("comparator_source", [](auto &p) { p.source_value[0] = RTLIL::State::S0; });
		reject_mutation("comparator_signedness", [](auto &p) { p.comparators[0].a_signed = !p.comparators[0].a_signed; });
		reject_mutation("removed_duplicate", [](auto &p) { p.comparators[0].removed_bit_indices.push_back(p.comparators[0].removed_bit_indices[0]); });
		reject_mutation("removed_out_of_range", [](auto &p) { p.comparators[0].removed_bit_indices[0] = p.comparators[0].full_width; });
		reject_mutation("comparator_fanout", [](auto &p) { p.comparators[0].comparator_full_fanout_snapshot.clear(); });
		reject_mutation("care_short", [](auto &p) { p.affected_sinks[0].care_mask_lsb_address.bits().pop_back(); });
		reject_mutation("value_short", [](auto &p) { p.affected_sinks[0].value_mask_lsb_address.bits().pop_back(); });
		reject_mutation("care_x", [](auto &p) { p.affected_sinks[0].care_mask_lsb_address.bits()[0] = RTLIL::State::Sx; });
		reject_mutation("value_z", [](auto &p) { p.affected_sinks[0].value_mask_lsb_address.bits()[0] = RTLIL::State::Sz; });
		reject_mutation("support_bound", [](auto &p) { while (p.support_lsb_first.size() <= 12) p.support_lsb_first.push_back(p.support_lsb_first[0]); });
		reject_mutation("dag_bound", [](auto &p) { p.normalized_dag.resize(4097); });
		reject_mutation("relation_authorization", [](auto &p) { p.decision_relations[0].reason_code = DecisionConeReasonCode::PriorityUnknown; });
		reject_mutation("duplicate_proof_id", [](auto &p) { p.proof_ids.push_back(p.proof_ids[0]); });

		DecisionConeProof all_zero_care = proof;
		for (DecisionSinkProof &sink : all_zero_care.affected_sinks)
			for (RTLIL::State &bit : sink.care_mask_lsb_address.bits()) bit = RTLIL::State::S0;
		DecisionPlan zero_plan;
		Case34RewriteResult zero_result;
		outcome.checks++;
		if (build_decision_plan(module, all_zero_care, zero_plan, zero_result) &&
				fingerprint(module) == baseline_fingerprint)
			outcome.passed++;
		else
			outcome.failures.push_back("full_length_all_zero_care_rejected_or_wrote");
		return outcome;
	}
	outcome.failures.push_back("no_valid_decision_cone_fixture");
	return outcome;
}

Case34RewriteResult run_case34_structural_rewrite(
		const std::vector<RTLIL::Module *> &input_modules)
{
	Case34RewriteResult result;
	std::vector<RTLIL::Module *> modules = input_modules;
	std::sort(modules.begin(), modules.end(), [](RTLIL::Module *lhs, RTLIL::Module *rhs) {
		return lhs->name.str() < rhs->name.str();
	});
	std::vector<DecisionPlan> decision_plans;
	std::vector<GuardedPlan> guarded_plans;
	std::vector<TablePlan> table_plans;
	for (RTLIL::Module *module : modules) {
		if (module->get_bool_attribute(ID::blackbox)) continue;
		DecisionConeProofRequest decision_request;
		decision_request.module_id = module->name.str();
		decision_request.module = module;
		DecisionConeProof decision_proof =
				make_decision_cone_analyzer()->analyze(decision_request);
		const bool decision_structure_seen =
				decision_proof.reason_code != DecisionConeReasonCode::StructureNotFound;
		DecisionPlan decision_plan;
		if (build_decision_plan(module, decision_proof, decision_plan, result))
			decision_plans.push_back(std::move(decision_plan));
		ProductionCase34ProofRequest request;
		request.module_id = module->name.str();
		request.module = module;
		std::unique_ptr<ProductionCase34Analyzer> analyzer = make_production_case34_analyzer();
		ProductionCase34ProofBundle bundle = analyzer->analyze(request);
		if (!decision_structure_seen)
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

	std::vector<DecisionPlan *> selected_decisions;
	std::vector<GuardedPlan *> selected_guarded;
	std::vector<TablePlan *> selected_tables;
	for (DecisionPlan &plan : decision_plans) {
		if (!claim(plan.module, plan.affected_cells)) {
			reject(result, "rewrite_conflict");
			continue;
		}
		if (!precheck(plan)) {
			reject(result, "snapshot_mismatch");
			continue;
		}
		selected_decisions.push_back(&plan);
	}
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

	for (DecisionPlan *plan : selected_decisions) {
		const int added = apply_decision_plan(*plan);
		Case34StructuralEdit edit;
		edit.module_id = plan->module->name.str();
		edit.rule = "guarded_next_state_decision_cone";
		edit.anchor_cell = plan->anchor_cell;
		edit.proof_reason = decision_cone_reason_code_name(plan->proof.reason_code);
		edit.added_cells = added;
		result.added_cells += added;
		result.edits.push_back(std::move(edit));
		result.committed_candidates++;
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
