#include "branch_delete.h"

#include <algorithm>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {

BranchRewriteBatch::CellPlan &BranchRewriteBatch::get_or_create_plan(
		const BranchProofCertificate &certificate)
{
	for (CellPlan &plan : plans_) {
		if (plan.module == certificate.module && plan.cell == certificate.cell) {
			if (plan.module_id != certificate.module_id || plan.cell_id != certificate.cell_id ||
					plan.cell_type != certificate.cell_type || plan.width != certificate.width ||
					plan.s_width != certificate.s_width ||
					plan.snapshot_a != certificate.snapshot_a ||
					plan.snapshot_b != certificate.snapshot_b ||
					plan.snapshot_s != certificate.snapshot_s ||
					plan.snapshot_y != certificate.snapshot_y)
				metadata_invalid_ = true;
			return plan;
		}
	}

	CellPlan plan;
	plan.module = certificate.module;
	plan.cell = certificate.cell;
	plan.module_id = certificate.module_id;
	plan.cell_id = certificate.cell_id;
	plan.cell_type = certificate.cell_type;
	plan.width = certificate.width;
	plan.s_width = certificate.s_width;
	plan.snapshot_a = certificate.snapshot_a;
	plan.snapshot_b = certificate.snapshot_b;
	plan.snapshot_s = certificate.snapshot_s;
	plan.snapshot_y = certificate.snapshot_y;
	plans_.push_back(plan);
	return plans_.back();
}

void BranchRewriteBatch::add(const BranchProofCertificate &certificate)
{
	stats_.certificates++;
	if (!certificate.proven_all_bits) {
		stats_.rejected_partial++;
		return;
	}
	if ((certificate.reason_code != "proved" && certificate.reason_code != "testing_mock") ||
			certificate.proof_id < 0) {
		stats_.rejected_unproved++;
		return;
	}

	CellPlan &plan = get_or_create_plan(certificate);
	if (certificate.module == nullptr || certificate.cell == nullptr ||
			certificate.cell_type != ID($pmux) || certificate.width <= 0 ||
			certificate.s_width <= 0 || certificate.branch_index < 0 ||
			certificate.branch_index >= certificate.s_width ||
			GetSize(certificate.snapshot_a) != certificate.width ||
			GetSize(certificate.snapshot_b) != certificate.width * certificate.s_width ||
			GetSize(certificate.snapshot_s) != certificate.s_width ||
			GetSize(certificate.snapshot_y) != certificate.width) {
		metadata_invalid_ = true;
		return;
	}

	auto inserted = plan.deletions.emplace(certificate.branch_index, certificate);
	if (!inserted.second)
		stats_.duplicate_targets++;
}

void BranchRewriteBatch::inject_first_duplicate_for_test()
{
	for (const CellPlan &plan : plans_) {
		if (!plan.deletions.empty()) {
			add(plan.deletions.begin()->second);
			return;
		}
	}
}

void BranchRewriteBatch::inject_conflict_for_test()
{
	forced_conflict_ = true;
	stats_.conflicts++;
}

void BranchRewriteBatch::corrupt_first_snapshot_for_test()
{
	if (!plans_.empty()) {
		plans_.front().s_width++;
		test_snapshot_corrupted_ = true;
	}
}

bool BranchRewriteBatch::commit(std::string &failure_reason)
{
	if (forced_conflict_) {
		failure_reason = "branch_target_conflict";
		return false;
	}
	if (metadata_invalid_) {
		failure_reason = "invalid_branch_rewrite_metadata";
		return false;
	}

	for (const CellPlan &plan : plans_) {
		if (plan.module == nullptr || plan.cell == nullptr ||
				plan.module->cell(plan.cell->name) != plan.cell) {
			failure_reason = "missing_branch_cell_at_commit";
			return false;
		}
		if (plan.cell->get_bool_attribute(ID::keep)) {
			failure_reason = "keep_attribute";
			return false;
		}
		if (plan.cell->type != ID($pmux) || plan.cell->type != plan.cell_type) {
			failure_reason = "branch_cell_type_changed";
			return false;
		}
		if (!plan.cell->hasParam(ID::WIDTH) || !plan.cell->hasParam(ID::S_WIDTH) ||
				plan.cell->getParam(ID::WIDTH).as_int() != plan.width ||
				plan.cell->getParam(ID::S_WIDTH).as_int() != plan.s_width) {
			failure_reason = test_snapshot_corrupted_ ?
					"test_branch_snapshot_mismatch" : "branch_parameter_snapshot_mismatch";
			return false;
		}
		for (RTLIL::IdString port : {ID::A, ID::B, ID::S, ID::Y}) {
			if (!plan.cell->hasPort(port)) {
				failure_reason = "missing_branch_port_at_commit";
				return false;
			}
		}
		if (plan.cell->getPort(ID::A) != plan.snapshot_a ||
				plan.cell->getPort(ID::B) != plan.snapshot_b ||
				plan.cell->getPort(ID::S) != plan.snapshot_s ||
				plan.cell->getPort(ID::Y) != plan.snapshot_y) {
			failure_reason = test_snapshot_corrupted_ ?
					"test_branch_snapshot_mismatch" : "branch_port_snapshot_mismatch";
			return false;
		}
	}

	for (CellPlan &plan : plans_) {
		if (plan.deletions.empty())
			continue;
		RTLIL::SigSpec retained_b, retained_s;
		std::vector<int> deleted;
		for (int branch = 0; branch < plan.s_width; ++branch) {
			if (plan.deletions.count(branch)) {
				deleted.push_back(branch);
				continue;
			}
			retained_b.append(plan.snapshot_b.extract(branch * plan.width, plan.width));
			retained_s.append(plan.snapshot_s.extract(branch, 1));
		}

		const int new_s_width = plan.s_width - GetSize(deleted);
		CommittedBranchEdit edit;
		edit.module_id = RTLIL::unescape_id(plan.module->name);
		edit.cell_id = RTLIL::unescape_id(plan.cell->name);
		edit.deleted_branches = deleted;
		edit.width = plan.width;
		for (int branch : deleted)
			edit.proof_ids.push_back(plan.deletions.at(branch).proof_id);
		edit.old_s_width = plan.s_width;
		edit.new_s_width = new_s_width;
		edit.old_type = "$pmux";

		if (new_s_width > 1) {
			plan.cell->setParam(ID::S_WIDTH, new_s_width);
			plan.cell->setPort(ID::B, retained_b);
			plan.cell->setPort(ID::S, retained_s);
			edit.new_type = "$pmux";
			edit.transformation = "shrink_pmux";
			stats_.cells_shrunk++;
		} else if (new_s_width == 1) {
			plan.cell->type = ID($mux);
			plan.cell->parameters.erase(ID::S_WIDTH);
			plan.cell->setPort(ID::B, retained_b);
			plan.cell->setPort(ID::S, retained_s);
			edit.new_type = "$mux";
			edit.transformation = "pmux_to_mux";
			stats_.cells_to_mux++;
		} else {
			plan.module->connect(plan.snapshot_y, plan.snapshot_a);
			plan.module->remove(plan.cell);
			edit.new_type = "connection";
			edit.transformation = "bypass_a";
			stats_.cells_bypassed++;
		}

		stats_.planned_branches += deleted.size();
		stats_.committed_branches += deleted.size();
		committed_edits_.push_back(edit);
	}

	failure_reason.clear();
	return true;
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
