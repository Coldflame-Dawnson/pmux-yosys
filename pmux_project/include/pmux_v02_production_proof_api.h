#ifndef PMUX_V02_PRODUCTION_PROOF_API_H
#define PMUX_V02_PRODUCTION_PROOF_API_H

#include "pmux_v02_case34_proof_api.h"

#include <memory>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

// A logical output bit may reach a physical sequential cell through data,
// enable or reset logic.  The full cell snapshot is the commit-time contract;
// q_bit identifies the physical state bit represented by logical_output_bit.
struct ProductionSequentialBitSnapshot {
	int logical_output_bit = -1;
	std::string cell_id;
	const RTLIL::Cell *cell = nullptr;
	int q_bit = -1;
	CellSnapshot snapshot;
	std::vector<GuardFact> table_mode_facts;
	std::vector<std::string> proof_ids;
};

struct ProductionFoldedDataBitConclusion {
	int comparator_index = -1;
	std::string cell_id;
	RTLIL::IdString port;
	int port_bit = -1;
	int source_bit_index = -1;
	RTLIL::State replacement = RTLIL::State::Sx;
	bool proved = false;
	std::string proof_id;
	V02ReasonCode reason_code = V02ReasonCode::DataBitUnknown;
};

struct ProductionPreservedDataBitConclusion {
	std::string cell_id;
	RTLIL::IdString port;
	int port_bit = -1;
	RTLIL::SigBit source_bit;
	bool preserved = false;
	std::string proof_id;
	V02ReasonCode reason_code = V02ReasonCode::PreservedBitMissing;
};

struct ProductionGuardedConeProof {
	std::string module;
	RTLIL::SigSpec source_value;
	int source_width = 0;
	std::vector<int> removed_bit_indices;
	std::vector<GuardFact> current_cycle_guard_facts;
	std::vector<CellSnapshot> guard_logic_cells;
	std::vector<ComparatorProofConclusion> comparator_cells;
	std::vector<ProductionFoldedDataBitConclusion> folded_data_bits;
	std::vector<ProductionPreservedDataBitConclusion> preserved_variable_bits;
	std::vector<CellSnapshot> affected_cells;
	std::vector<ProductionSequentialBitSnapshot> sequential_sinks;
	ResetEnableHoldContract reset_enable_hold_contract;
	bool all_comparators_equivalent = false;
	bool all_data_bits_proved = false;
	bool all_fanout_covered = false;
	bool all_sequential_boundaries_proved = false;
	bool proven_all_obligations = false;
	std::vector<std::string> proof_ids;
	V02ProofStatus status = V02ProofStatus::Unknown;
	V02ReasonCode reason_code = V02ReasonCode::InvalidRequest;
	ResourceUsage usage;
};

struct ProductionConstantTableProof {
	std::string module;
	RTLIL::SigSpec logical_root_outputs;
	std::vector<RTLIL::SigBit> address_bits_lsb_first;
	std::vector<RTLIL::SigBit> output_bits_lsb_first;
	int address_width = 0;
	int output_width = 0;
	std::vector<RTLIL::Const> truth_table;
	std::vector<CellSnapshot> source_cells;
	std::vector<ProductionSequentialBitSnapshot> sequential_sinks;
	ResetEnableHoldContract reset_enable_hold_contract;
	bool complete_binary_domain = false;
	bool proven_all_rows = false;
	bool all_output_bits_mapped = false;
	bool proven_all_obligations = false;
	std::vector<std::string> proof_ids;
	V02ProofStatus status = V02ProofStatus::Unknown;
	V02ReasonCode reason_code = V02ReasonCode::InvalidRequest;
	ResourceUsage usage;
};

struct ProductionCase34ProofRequest {
	std::string module_id;
	const RTLIL::Module *module = nullptr;
	// Optional commit-time anchors. Empty vectors request structural discovery;
	// non-empty vectors must match the discovered order/snapshots exactly.
	std::vector<RTLIL::SigBit> expected_address_bits_lsb_first;
	std::vector<RTLIL::SigBit> expected_output_bits_lsb_first;
	std::vector<ProductionSequentialBitSnapshot> expected_sequential_sinks;
	int max_address_width = 8;
	int max_output_width = 64;
	AnalysisBudget budget;
};

struct ProductionCase34ProofBundle {
	std::string module;
	std::vector<ProductionGuardedConeProof> guarded_cones;
	std::vector<ProductionConstantTableProof> constant_tables;
	V02ProofStatus status = V02ProofStatus::Unknown;
	V02ReasonCode reason_code = V02ReasonCode::InvalidRequest;
	ResourceUsage usage;
};

class ProductionCase34Analyzer {
public:
	virtual ~ProductionCase34Analyzer() = default;
	virtual ProductionCase34ProofBundle analyze(const ProductionCase34ProofRequest &request) const = 0;
};

std::unique_ptr<ProductionCase34Analyzer> make_production_case34_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
