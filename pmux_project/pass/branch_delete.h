#ifndef PMUX_OPT_BRANCH_DELETE_H
#define PMUX_OPT_BRANCH_DELETE_H

#include "kernel/yosys.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

// Temporary B-side certificate shape. A future proof provider may construct
// this object, but the rewriter never infers equivalence by itself.
struct BranchProofCertificate {
	RTLIL::Module *module = nullptr;
	RTLIL::Cell *cell = nullptr;
	std::string module_id;
	std::string cell_id;
	int branch_index = -1;
	int width = 0;
	int s_width = 0;
	RTLIL::IdString cell_type;
	RTLIL::SigSpec snapshot_a;
	RTLIL::SigSpec snapshot_b;
	RTLIL::SigSpec snapshot_s;
	RTLIL::SigSpec snapshot_y;
	bool proven_all_bits = false;
	int proof_id = -1;
	std::string reason_code;
};

struct BranchRewriteStats {
	std::size_t certificates = 0;
	std::size_t rejected_partial = 0;
	std::size_t rejected_unproved = 0;
	std::size_t duplicate_targets = 0;
	std::size_t conflicts = 0;
	std::size_t planned_branches = 0;
	std::size_t committed_branches = 0;
	std::size_t cells_shrunk = 0;
	std::size_t cells_to_mux = 0;
	std::size_t cells_bypassed = 0;
};

struct CommittedBranchEdit {
	std::string module_id;
	std::string cell_id;
	std::vector<int> deleted_branches;
	std::vector<int> proof_ids;
	int width = 0;
	int old_s_width = 0;
	int new_s_width = 0;
	std::string old_type;
	std::string new_type;
	std::string transformation;
};

class BranchRewriteBatch
{
public:
	void add(const BranchProofCertificate &certificate);
	void inject_first_duplicate_for_test();
	void inject_conflict_for_test();
	void corrupt_first_snapshot_for_test();
	bool commit(std::string &failure_reason);

	const BranchRewriteStats &stats() const { return stats_; }
	const std::vector<CommittedBranchEdit> &committed_edits() const { return committed_edits_; }
	bool empty() const { return plans_.empty(); }

private:
	struct CellPlan {
		RTLIL::Module *module = nullptr;
		RTLIL::Cell *cell = nullptr;
		std::string module_id;
		std::string cell_id;
		RTLIL::IdString cell_type;
		int width = 0;
		int s_width = 0;
		RTLIL::SigSpec snapshot_a;
		RTLIL::SigSpec snapshot_b;
		RTLIL::SigSpec snapshot_s;
		RTLIL::SigSpec snapshot_y;
		std::map<int, BranchProofCertificate> deletions;
	};

	std::vector<CellPlan> plans_;
	BranchRewriteStats stats_;
	std::vector<CommittedBranchEdit> committed_edits_;
	bool metadata_invalid_ = false;
	bool forced_conflict_ = false;
	bool test_snapshot_corrupted_ = false;

	CellPlan &get_or_create_plan(const BranchProofCertificate &certificate);
};

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
