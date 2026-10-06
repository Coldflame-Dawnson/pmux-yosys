#ifndef PMUX_OPT_API_H
#define PMUX_OPT_API_H

#include "kernel/yosys.h"

#include <cstddef>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

enum class PhysicalPort {
	A,
	B,
};

enum class EntryKind {
	MuxA,
	MuxB,
	PmuxA,
	PmuxB,
};

enum class FactValue {
	Zero,
	One,
	Unknown,
};

enum class EntryStatus {
	Normal,
	FactConflict,
	BudgetExhausted,
	InvalidRequest,
};

struct AnalysisBudget {
	std::size_t max_cells = 512;
	std::size_t max_bits = 8192;
	std::size_t max_events = 262144;
};

struct GuardFact {
	RTLIL::SigBit bit;
	RTLIL::State value = RTLIL::State::Sx;
};

struct EntryRequest {
	std::string module_id;
	std::string cell_id;
	const RTLIL::Module *module = nullptr;
	const RTLIL::Cell *cell = nullptr;
	PhysicalPort physical_port = PhysicalPort::A;
	EntryKind entry_kind = EntryKind::MuxA;
	int branch_index = -1;
	int physical_offset = 0;
	int width = 0;
	RTLIL::SigSpec data;
	RTLIL::SigSpec physical_port_snapshot;
	std::vector<GuardFact> guard;
	AnalysisBudget budget;
};

struct BitConclusion {
	int bit_index = -1;
	FactValue value = FactValue::Unknown;
	int proof_id = -1;
	std::string reason_code;
};

struct ProofRecord {
	int proof_id = -1;
	RTLIL::SigBit target;
	FactValue value = FactValue::Unknown;
	std::string rule;
	std::vector<GuardFact> direct_premises;
	std::string source_cell_id;
	std::vector<int> parent_proof_ids;
};

struct ResourceUsage {
	std::size_t visited_cells = 0;
	std::size_t visited_bits = 0;
	std::size_t propagation_events = 0;
};

struct EntryAnalysisResult {
	EntryStatus status = EntryStatus::Normal;
	std::vector<BitConclusion> bits;
	std::vector<ProofRecord> proof_records;
	ResourceUsage usage;
	std::string reason_code;
};

class EntryAnalyzer {
public:
	virtual ~EntryAnalyzer() = default;
	virtual EntryAnalysisResult analyze(const EntryRequest &request) const = 0;
};

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
