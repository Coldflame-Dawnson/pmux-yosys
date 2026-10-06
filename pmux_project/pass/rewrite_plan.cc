#include "rewrite_plan.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

static RTLIL::IdString port_id(PhysicalPort port)
{
	return port == PhysicalPort::A ? ID::A : ID::B;
}

static std::string make_entry_id(const EntryRequest &request)
{
	return request.module_id + "|" + request.cell_id + "|" +
			std::to_string(int(request.entry_kind)) + "|" +
			std::to_string(request.branch_index);
}

PortRewritePlan &RewriteBatch::get_or_create_plan(const EntryRequest &request)
{
	for (auto &plan : plans_) {
		if (plan.module == request.module && plan.cell == request.cell &&
				plan.physical_port == request.physical_port) {
			if (plan.snapshot != request.physical_port_snapshot ||
					plan.port_width != GetSize(request.physical_port_snapshot))
				metadata_invalid_ = true;
			return plan;
		}
	}

	PortRewritePlan plan;
	plan.module = const_cast<RTLIL::Module *>(request.module);
	plan.cell = const_cast<RTLIL::Cell *>(request.cell);
	plan.physical_port = request.physical_port;
	plan.cell_type = request.cell == nullptr ? RTLIL::IdString() : request.cell->type;
	plan.snapshot = request.physical_port_snapshot;
	plan.port_width = GetSize(request.physical_port_snapshot);
	plans_.push_back(plan);
	return plans_.back();
}

void RewriteBatch::add(const EntryRequest &request, const EntryAnalysisResult &result)
{
	if (result.status == EntryStatus::FactConflict ||
			result.status == EntryStatus::InvalidRequest) {
		stats_.rejected_results++;
		return;
	}

	PortRewritePlan &plan = get_or_create_plan(request);
	const std::string source_entry_id = make_entry_id(request);

	if (request.module == nullptr || request.cell == nullptr ||
			request.width != GetSize(request.data) || request.physical_offset < 0 ||
			request.physical_offset + request.width > plan.port_width) {
		metadata_invalid_ = true;
		return;
	}

	if (request.physical_port == PhysicalPort::A && request.physical_offset != 0) {
		metadata_invalid_ = true;
		return;
	}

	for (const BitConclusion &conclusion : result.bits) {
		if (conclusion.value == FactValue::Unknown)
			continue;
		if ((conclusion.reason_code != "proved" && conclusion.reason_code != "testing_stub") ||
				conclusion.proof_id < 0) {
			stats_.rejected_unproved_conclusions++;
			continue;
		}
		if (conclusion.bit_index < 0 || conclusion.bit_index >= request.width) {
			metadata_invalid_ = true;
			continue;
		}

		const RTLIL::SigBit original = request.data[conclusion.bit_index];
		if (original.wire == nullptr) {
			if (original.data == RTLIL::State::Sx || original.data == RTLIL::State::Sz)
				stats_.skipped_original_xz++;
			else
				stats_.skipped_original_binary_constants++;
			continue;
		}

		const int physical_bit = request.physical_offset + conclusion.bit_index;
		if (plan.snapshot[physical_bit] != original) {
			metadata_invalid_ = true;
			continue;
		}

		RewriteCandidate candidate;
		candidate.physical_bit = physical_bit;
		candidate.original = original;
		candidate.target = conclusion.value == FactValue::Zero ?
				RTLIL::State::S0 : RTLIL::State::S1;
		candidate.source_entry_id = source_entry_id;
		candidate.proof_id = conclusion.proof_id;
		candidate.branch_index = request.branch_index;
		candidate.branch_bit = conclusion.bit_index;
		plan.candidates.push_back(candidate);
		stats_.proven_bits++;
	}
}

void RewriteBatch::corrupt_first_snapshot_for_test()
{
	if (!plans_.empty()) {
		plans_.front().port_width++;
		test_snapshot_corrupted_ = true;
	}
}

void RewriteBatch::inject_first_conflict_for_test()
{
	for (auto &plan : plans_) {
		if (plan.candidates.empty())
			continue;
		RewriteCandidate conflict = plan.candidates.front();
		conflict.target = conflict.target == RTLIL::State::S0 ?
				RTLIL::State::S1 : RTLIL::State::S0;
		conflict.source_entry_id += "|test_conflict";
		plan.candidates.push_back(conflict);
		return;
	}
}

void RewriteBatch::inject_first_duplicate_for_test()
{
	for (auto &plan : plans_) {
		if (plan.candidates.empty())
			continue;
		RewriteCandidate duplicate = plan.candidates.front();
		duplicate.source_entry_id += "|test_duplicate";
		plan.candidates.push_back(duplicate);
		return;
	}
}

bool RewriteBatch::commit(std::string &failure_reason)
{
	if (metadata_invalid_) {
		failure_reason = "invalid_rewrite_metadata";
		return false;
	}

	std::vector<std::vector<RewriteCandidate>> accepted(plans_.size());
	std::set<std::string> all_abandoned_entries;

	for (std::size_t plan_index = 0; plan_index < plans_.size(); ++plan_index) {
		const PortRewritePlan &plan = plans_[plan_index];
		std::map<int, std::vector<const RewriteCandidate *>> by_bit;
		for (const RewriteCandidate &candidate : plan.candidates)
			by_bit[candidate.physical_bit].push_back(&candidate);

		std::set<std::string> abandoned_entries;
		for (const auto &item : by_bit) {
			const auto &group = item.second;
			if (group.empty())
				continue;
			const RTLIL::State expected = group.front()->target;
			bool conflict = false;
			for (const RewriteCandidate *candidate : group)
				conflict = conflict || candidate->target != expected;
			if (conflict) {
				stats_.rewrite_conflicts++;
				for (const RewriteCandidate *candidate : group) {
					abandoned_entries.insert(candidate->source_entry_id);
					all_abandoned_entries.insert(candidate->source_entry_id);
				}
			}
		}

		std::map<int, RewriteCandidate> unique_by_bit;
		for (const RewriteCandidate &candidate : plan.candidates) {
			if (abandoned_entries.count(candidate.source_entry_id))
				continue;
			auto inserted = unique_by_bit.emplace(candidate.physical_bit, candidate);
			if (!inserted.second) {
				if (inserted.first->second.target != candidate.target) {
					failure_reason = "unresolved_rewrite_conflict";
					return false;
				}
				stats_.duplicate_targets++;
			}
		}

		for (const auto &item : unique_by_bit)
			accepted[plan_index].push_back(item.second);
	}

	stats_.abandoned_entries = all_abandoned_entries.size();
	for (const std::string &entry_id : all_abandoned_entries)
		abandoned_entries_.push_back({entry_id, "rewrite_conflict"});
	for (const auto &items : accepted)
		stats_.planned_bits += items.size();

	for (std::size_t plan_index = 0; plan_index < plans_.size(); ++plan_index) {
		if (accepted[plan_index].empty())
			continue;
		const PortRewritePlan &plan = plans_[plan_index];
		if (plan.module == nullptr || plan.cell == nullptr ||
				plan.module->cell(plan.cell->name) != plan.cell) {
			failure_reason = "missing_cell_at_commit";
			return false;
		}
		if (plan.cell->type != plan.cell_type) {
			failure_reason = "cell_type_changed";
			return false;
		}
		const RTLIL::IdString id = port_id(plan.physical_port);
		if (!plan.cell->hasPort(id)) {
			failure_reason = "missing_port_at_commit";
			return false;
		}
		const RTLIL::SigSpec current = plan.cell->getPort(id);
		if (GetSize(current) != plan.port_width || current != plan.snapshot) {
			failure_reason = test_snapshot_corrupted_ ?
					"test_snapshot_mismatch" : "snapshot_mismatch";
			return false;
		}
		for (const RewriteCandidate &candidate : accepted[plan_index]) {
			if (candidate.physical_bit < 0 || candidate.physical_bit >= plan.port_width ||
					current[candidate.physical_bit] != candidate.original) {
				failure_reason = "bit_snapshot_mismatch";
				return false;
			}
		}
	}

	for (std::size_t plan_index = 0; plan_index < plans_.size(); ++plan_index) {
		if (accepted[plan_index].empty())
			continue;
		PortRewritePlan &plan = plans_[plan_index];
		RTLIL::SigSpec updated = plan.snapshot;
		for (const RewriteCandidate &candidate : accepted[plan_index])
			updated.replace(candidate.physical_bit, RTLIL::SigSpec(candidate.target));
		plan.cell->setPort(port_id(plan.physical_port), updated);
		for (const RewriteCandidate &candidate : accepted[plan_index]) {
			CommittedEdit edit;
			edit.module_id = RTLIL::unescape_id(plan.module->name);
			edit.cell_id = RTLIL::unescape_id(plan.cell->name);
			edit.physical_port = plan.physical_port;
			edit.physical_bit = candidate.physical_bit;
			edit.original = candidate.original;
			edit.target = candidate.target;
			edit.branch_index = plan.cell_type == ID($pmux) &&
					plan.physical_port == PhysicalPort::B ? candidate.branch_index : -1;
			edit.branch_bit = candidate.branch_bit;
			edit.proof_id = candidate.proof_id;
			edit.source_entry_id = candidate.source_entry_id;
			committed_edits_.push_back(edit);
		}
		stats_.written_bits += accepted[plan_index].size();
	}

	failure_reason.clear();
	return true;
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
