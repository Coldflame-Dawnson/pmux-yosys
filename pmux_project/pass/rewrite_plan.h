#ifndef PMUX_OPT_REWRITE_PLAN_H
#define PMUX_OPT_REWRITE_PLAN_H

#include "pmux_opt_api.h"

#include <cstddef>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

struct RewriteCandidate {
	int physical_bit = -1;
	RTLIL::SigBit original;
	RTLIL::State target = RTLIL::State::Sx;
	std::string source_entry_id;
	int proof_id = -1;
	int branch_index = -1;
	int branch_bit = -1;
};

struct PortRewritePlan {
	RTLIL::Module *module = nullptr;
	RTLIL::Cell *cell = nullptr;
	PhysicalPort physical_port = PhysicalPort::A;
	RTLIL::IdString cell_type;
	RTLIL::SigSpec snapshot;
	int port_width = 0;
	std::vector<RewriteCandidate> candidates;
};

struct RewriteStats {
	std::size_t proven_bits = 0;
	std::size_t skipped_original_constants = 0;
	std::size_t rejected_results = 0;
	std::size_t duplicate_targets = 0;
	std::size_t rewrite_conflicts = 0;
	std::size_t abandoned_entries = 0;
	std::size_t planned_bits = 0;
	std::size_t written_bits = 0;
};

class RewriteBatch
{
public:
	void add(const EntryRequest &request, const EntryAnalysisResult &result);
	void inject_first_duplicate_for_test();
	void inject_first_conflict_for_test();
	void corrupt_first_snapshot_for_test();
	bool commit(std::string &failure_reason);
	const RewriteStats &stats() const { return stats_; }

private:
	std::vector<PortRewritePlan> plans_;
	RewriteStats stats_;
	bool metadata_invalid_ = false;
	bool test_snapshot_corrupted_ = false;

	PortRewritePlan &get_or_create_plan(const EntryRequest &request);
};

} // namespace PmuxOpt

YOSYS_NAMESPACE_END

#endif
