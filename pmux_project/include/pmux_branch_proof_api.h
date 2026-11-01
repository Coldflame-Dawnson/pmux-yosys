#ifndef PMUX_BRANCH_PROOF_API_H
#define PMUX_BRANCH_PROOF_API_H

#include "pmux_opt_api.h"

#include <memory>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

enum class BranchFallbackKind {
	DefaultA,
	LowerPriorityBranch,
	Unresolved,
};

enum class BranchBitStatus {
	ProvenEqual,
	Unknown,
	FactConflict,
	BudgetExhausted,
};

struct BranchProofRequest {
	std::string module_id;
	std::string cell_id;
	const RTLIL::Module *module = nullptr;
	const RTLIL::Cell *cell = nullptr;
	int branch_index = -1;
	RTLIL::SigSpec a_snapshot;
	RTLIL::SigSpec b_snapshot;
	RTLIL::SigSpec s_snapshot;
	RTLIL::SigSpec y_snapshot;
	AnalysisBudget budget;
};

struct BranchBitProof {
	int bit_index = -1;
	BranchBitStatus status = BranchBitStatus::Unknown;
	FactValue branch_value = FactValue::Unknown;
	FactValue fallback_value = FactValue::Unknown;
	int branch_proof_id = -1;
	int fallback_proof_id = -1;
	std::string reason_code;
};

// Frozen v0.2-B result consumed read-only by role B.  A true
// proven_all_bits is the only authorization to plan whole-branch deletion.
struct BranchProof {
	std::string cell;
	int branch_index = -1;
	int width = 0;
	std::vector<GuardFact> effective_guard;
	BranchFallbackKind fallback_kind = BranchFallbackKind::Unresolved;
	int fallback_branch_index = -1;
	bool proven_all_bits = false;
	std::vector<BranchBitProof> per_bit_status;
	std::string reason_code;

	EntryStatus status = EntryStatus::Normal;
	ResourceUsage usage;
	std::vector<ProofRecord> proof_records;
};

class BranchRedundancyAnalyzer {
public:
	virtual ~BranchRedundancyAnalyzer() = default;
	virtual BranchProof prove(const BranchProofRequest &request) const = 0;
};

std::unique_ptr<BranchRedundancyAnalyzer> make_branch_redundancy_analyzer();

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
