#ifndef PMUX_V02_DECISION_CONE_PROOF_API_H
#define PMUX_V02_DECISION_CONE_PROOF_API_H

#include "pmux_v02_production_proof_api.h"

#include <memory>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

// Stable, machine-readable outcomes for the complete next-state decision-cone
// proof.  Every value except Proved is non-authorizing.
enum class DecisionConeReasonCode {
	Proved,
	StructureNotFound,
	GuardMissing,
	GuardNotDominating,
	GuardPartial,
	GuardXZ,
	ComparisonWidthUnknown,
	ComparisonSignednessUnknown,
	SelectOverlapUnproved,
	PriorityUnknown,
	DataBitUnknown,
	PartialSinkProof,
	UncoveredFanout,
	MultipleDriver,
	AliasUnknown,
	UnsupportedCell,
	SequentialBoundaryChanged,
	ResetEnableHoldUnknown,
	SnapshotMismatch,
	FactConflict,
	BudgetCellsExhausted,
	BudgetBitsExhausted,
	BudgetEventsExhausted,
	InvalidRequest,
};

const char *decision_cone_reason_code_name(DecisionConeReasonCode code);

enum class DecisionDagNodeKind {
	Constant0,
	Constant1,
	Test,
};

// Nodes are returned in topological order.  Constant nodes have no inputs.
// Test implements input[input_index] ? high_node : low_node, and both child
// indices must be smaller than node_id.
struct DecisionDagNode {
	int node_id = -1;
	DecisionDagNodeKind kind = DecisionDagNodeKind::Constant0;
	int input_index = -1;
	int low_node = -1;
	int high_node = -1;
	std::string proof_id;
};

struct DecisionSupportBit {
	int input_index = -1;
	RTLIL::SigBit bit;
	bool module_input = false;
	bool sequential_q = false;
	std::string sequential_cell_id;
	int sequential_q_bit = -1;
	std::string proof_id;
};

enum class DecisionRelationKind {
	MuxPriority,
	PmuxMutuallyExclusive,
	SequentialControlPriority,
};

// This is an observed structural relation, not a source-code reconstruction.
// branch_order is low-to-high assignment priority.  For native $pmux,
// pairwise_disjoint must be true because overlapping selects are x/undefined.
struct DecisionRelationProof {
	DecisionRelationKind kind = DecisionRelationKind::MuxPriority;
	std::string cell_id;
	CellSnapshot snapshot;
	std::vector<RTLIL::SigBit> selects_in_branch_order;
	bool pairwise_disjoint = false;
	bool overlap_checked = false;
	bool last_assignment_semantics_proved = false;
	std::string proof_id;
	DecisionConeReasonCode reason_code = DecisionConeReasonCode::PriorityUnknown;
};

struct DecisionAttributeSnapshot {
	RTLIL::IdString attribute;
	RTLIL::Const value;
};

// CellSnapshot freezes type/parameters/ports.  This decision-cone-specific
// wrapper additionally freezes cell and Q-wire attributes so initialization
// metadata cannot be lost or silently changed by a consumer.
struct DecisionSequentialBitSnapshot {
	int logical_sink_bit = -1;
	std::string cell_id;
	const RTLIL::Cell *cell = nullptr;
	int d_bit = -1;
	int q_bit = -1;
	CellSnapshot cell_snapshot;
	std::vector<DecisionAttributeSnapshot> cell_attributes;
	std::vector<DecisionAttributeSnapshot> q_wire_attributes;
	int q_wire_port_id = 0;
	bool q_wire_port_input = false;
	bool q_wire_port_output = false;
	std::string proof_id;
};

struct DecisionSinkProof {
	int logical_sink_bit = -1;
	std::string cell_id;
	const RTLIL::Cell *cell = nullptr;
	int d_bit = -1;
	int q_bit = -1;
	RTLIL::SigBit original_d;
	DecisionSequentialBitSnapshot sequential_snapshot;
	std::vector<GuardFact> observation_guard_facts;
	// Bit i corresponds to the binary address i over support_lsb_first.  care=0
	// is an unobserved D value (hold or dominating reset), never an equivalence
	// shortcut.  value bits are meaningful only where care=1.
	RTLIL::Const care_mask_lsb_address;
	RTLIL::Const value_mask_lsb_address;
	int normalized_root_node = -1;
	std::vector<CellSnapshot> original_d_cone;
	std::vector<CellSnapshot> external_consumers;
	bool equivalent_on_all_care_rows = false;
	bool guard_outside_behavior_preserved = false;
	bool default_path_preserved = false;
	bool hold_path_preserved = false;
	bool reset_path_preserved = false;
	bool enable_path_preserved = false;
	bool initialization_preserved = false;
	bool no_cross_dff_propagation = false;
	bool all_fanout_covered = false;
	std::string proof_id;
	DecisionConeReasonCode reason_code = DecisionConeReasonCode::PartialSinkProof;
};

struct DecisionConeProof {
	std::string module;
	RTLIL::SigSpec source_value;
	std::vector<int> guard_fixed_source_bits;
	std::vector<GuardFact> outer_guard_facts;
	std::vector<DecisionSupportBit> support_lsb_first;
	std::vector<ComparatorProofConclusion> comparators;
	std::vector<DecisionRelationProof> decision_relations;
	std::vector<DecisionDagNode> normalized_dag;
	std::vector<DecisionSinkProof> affected_sinks;
	std::vector<DecisionSequentialBitSnapshot> unaffected_sequential_bits;
	std::vector<CellSnapshot> source_cells;
	std::vector<CellSnapshot> external_consumers;
	ResetEnableHoldContract reset_enable_hold_contract;
	bool structure_complete = false;
	bool current_cycle_guard_proved = false;
	bool selection_semantics_proved = false;
	bool guard_inside_equivalence_proved = false;
	bool guard_outside_equivalence_proved = false;
	bool all_sink_bits_proved = false;
	bool all_fanout_covered = false;
	bool all_sequential_boundaries_proved = false;
	bool normalized_dag_complete = false;
	bool proven_all_obligations = false;
	std::vector<std::string> proof_ids;
	V02ProofStatus status = V02ProofStatus::Unknown;
	DecisionConeReasonCode reason_code = DecisionConeReasonCode::InvalidRequest;
	ResourceUsage usage;
};

struct DecisionConeProofRequest {
	std::string module_id;
	const RTLIL::Module *module = nullptr;
	// Optional commit-time anchors.  Non-empty vectors must match the proof
	// result exactly; mismatches are non-authorizing.
	std::vector<RTLIL::SigBit> expected_support_lsb_first;
	std::vector<DecisionSequentialBitSnapshot> expected_sequential_sinks;
	std::vector<CellSnapshot> expected_source_cells;
	int max_support_bits = 12;
	int max_dag_nodes = 4096;
	AnalysisBudget budget;
};

class DecisionConeAnalyzer {
public:
	virtual ~DecisionConeAnalyzer() = default;
	virtual DecisionConeProof analyze(const DecisionConeProofRequest &request) const = 0;
};

std::unique_ptr<DecisionConeAnalyzer> make_decision_cone_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
