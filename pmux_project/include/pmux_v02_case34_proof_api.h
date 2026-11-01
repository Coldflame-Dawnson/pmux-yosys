#ifndef PMUX_V02_CASE34_PROOF_API_H
#define PMUX_V02_CASE34_PROOF_API_H

#include "pmux_opt_api.h"

#include <memory>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

// Normal is deliberately not synonymous with a partial proof.  A consumer
// may authorize a rewrite only when status is Normal and the corresponding
// proven_all_* member is true.
enum class V02ProofStatus {
	Normal,
	Unknown,
	FactConflict,
	BudgetExhausted,
	InvalidRequest,
};

enum class V02ReasonCode {
	Proved,
	GuardNotDominating,
	GuardUnknown,
	GuardXz,
	ComparisonWidthUnknown,
	ComparisonSignednessUnknown,
	ComparatorFanoutUncovered,
	FullReducedCompareNotEquivalent,
	DataBitUnknown,
	PartialDataFold,
	PreservedBitMissing,
	TableExtractFailed,
	AddressOrderUnknown,
	OutputOrderUnknown,
	TableIncomplete,
	TableValueConflict,
	TableXz,
	MultipleDriver,
	SequentialBoundaryChanged,
	ResetEnableHoldUnknown,
	FactConflict,
	BudgetCellsExhausted,
	BudgetBitsExhausted,
	BudgetEventsExhausted,
	SatCounterexample,
	SatUnknown,
	SatTimeout,
	InvalidRequest,
	SnapshotMismatch,
};

const char *v02_reason_code_name(V02ReasonCode code);

struct CellPortSnapshot {
	RTLIL::IdString port;
	RTLIL::SigSpec signal;
};

struct CellParameterSnapshot {
	RTLIL::IdString parameter;
	RTLIL::Const value;
};

struct CellSnapshot {
	std::string cell_id;
	RTLIL::IdString type;
	std::vector<CellPortSnapshot> ports;
	std::vector<CellParameterSnapshot> parameters;
};

struct FanoutUseSnapshot {
	std::string cell_id;
	RTLIL::IdString port;
	int port_bit = -1;
	RTLIL::SigBit canonical_bit;
};

struct ComparatorNarrowingRequest {
	std::string cell_id;
	const RTLIL::Cell *cell = nullptr;
	RTLIL::SigSpec source_snapshot;
	RTLIL::Const constant_snapshot;
	std::vector<int> removed_bit_indices;
	std::vector<FanoutUseSnapshot> full_fanout_snapshot;
};

struct GuardedDataBitRequest {
	int result_bit_index = -1;
	RTLIL::SigBit source_bit;
	RTLIL::State replacement = RTLIL::State::Sx;
	// -1 means the outer guard alone.  Otherwise the named comparator result
	// is additionally assumed true for this selected-data obligation.
	int comparator_index = -1;
};

struct PreservedVariableBitRequest {
	int result_bit_index = -1;
	RTLIL::SigBit source_bit;
};

struct GuardedConeProofRequest {
	std::string module_id;
	const RTLIL::Module *module = nullptr;
	std::string guard_sink_cell_id;
	const RTLIL::Cell *guard_sink_cell = nullptr;
	std::vector<GuardFact> guard_facts;
	RTLIL::SigSpec source_value;
	int source_width = 0;
	std::vector<int> removed_bit_indices;
	std::vector<ComparatorNarrowingRequest> comparators;
	std::vector<GuardedDataBitRequest> folded_data_bits;
	std::vector<PreservedVariableBitRequest> preserved_variable_bits;
	std::vector<std::string> affected_cells;
	AnalysisBudget budget;
};

struct ComparatorProofConclusion {
	std::string cell_id;
	RTLIL::Const comparator_constant;
	int full_width = 0;
	int reduced_width = 0;
	bool a_signed = false;
	bool b_signed = false;
	std::vector<int> removed_bit_indices;
	std::vector<FanoutUseSnapshot> comparator_full_fanout_snapshot;
	bool equivalent_under_guard = false;
	bool comparator_must_be_preserved = true;
	std::vector<int> proof_ids;
	V02ReasonCode reason_code = V02ReasonCode::FullReducedCompareNotEquivalent;
};

enum class GuardedDataBitStatus {
	FoldedZero,
	FoldedOne,
	PreservedVariable,
	Unknown,
};

struct GuardedDataBitConclusion {
	int result_bit_index = -1;
	RTLIL::SigBit source_bit;
	GuardedDataBitStatus status = GuardedDataBitStatus::Unknown;
	int proof_id = -1;
	V02ReasonCode reason_code = V02ReasonCode::DataBitUnknown;
};

struct GuardedConeProof {
	std::string module;
	std::string guard_sink_cell;
	std::vector<GuardFact> guard_facts;
	RTLIL::SigSpec source_value;
	int source_width = 0;
	std::vector<int> removed_bit_indices;
	std::vector<ComparatorProofConclusion> comparator_cells;
	std::vector<RTLIL::Const> comparator_constants;
	std::vector<std::vector<FanoutUseSnapshot>> comparator_full_fanout_snapshot;
	std::vector<bool> per_comparator_equivalent_under_guard;
	std::vector<GuardedDataBitConclusion> folded_data_bits;
	std::vector<GuardedDataBitConclusion> preserved_variable_bits;
	std::vector<std::string> affected_cells;
	bool proven_all_obligations = false;
	std::vector<int> proof_ids;
	V02ProofStatus status = V02ProofStatus::Unknown;
	V02ReasonCode reason_code = V02ReasonCode::InvalidRequest;
	ResourceUsage usage;
	std::vector<ProofRecord> proof_records;
};

class GuardedConeAnalyzer {
public:
	virtual ~GuardedConeAnalyzer() = default;
	virtual GuardedConeProof prove(const GuardedConeProofRequest &request) const = 0;
};

struct SequentialSinkRequest {
	std::string cell_id;
	const RTLIL::Cell *cell = nullptr;
	std::vector<int> root_output_bit_indices;
	CellSnapshot snapshot;
};

struct ConstantTableProofRequest {
	std::string module_id;
	const RTLIL::Module *module = nullptr;
	RTLIL::SigSpec root_outputs;
	std::vector<RTLIL::SigBit> address_bits_lsb_first;
	std::vector<RTLIL::SigBit> output_bits_lsb_first;
	std::vector<std::string> source_cells;
	std::vector<SequentialSinkRequest> sequential_sinks;
	AnalysisBudget budget;
};

struct SequentialSinkSnapshot {
	std::string cell_id;
	CellSnapshot cell;
	std::vector<int> root_output_bit_indices;
};

struct ResetEnableHoldContract {
	bool no_cross_dff_propagation = false;
	bool reset_preserved = false;
	bool enable_preserved = false;
	bool hold_preserved = false;
	std::vector<std::string> proof_ids;
};

// truth_table[row][bit] uses RTLIL's ordinary LSB-first Const indexing.
// row is the unsigned address whose bit i is address_bits_lsb_first[i].
// Only S0/S1 are valid in an authorized result; Sx/Sz are rejected.
struct ConstantTableProof {
	std::string module;
	RTLIL::SigSpec root_outputs;
	std::vector<RTLIL::SigBit> address_bits_lsb_first;
	std::vector<RTLIL::SigBit> output_bits_lsb_first;
	int address_width = 0;
	int output_width = 0;
	std::vector<RTLIL::Const> truth_table;
	std::vector<std::string> source_cells;
	std::vector<SequentialSinkSnapshot> sequential_sinks;
	ResetEnableHoldContract reset_enable_hold_contract;
	bool complete_binary_domain = false;
	bool proven_all_rows = false;
	std::vector<std::string> proof_ids;
	V02ProofStatus status = V02ProofStatus::Unknown;
	V02ReasonCode reason_code = V02ReasonCode::InvalidRequest;
	ResourceUsage usage;
};

class ConstantTableAnalyzer {
public:
	virtual ~ConstantTableAnalyzer() = default;
	virtual ConstantTableProof prove(const ConstantTableProofRequest &request) const = 0;
};

std::unique_ptr<GuardedConeAnalyzer> make_guarded_cone_analyzer();
std::unique_ptr<ConstantTableAnalyzer> make_constant_table_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
