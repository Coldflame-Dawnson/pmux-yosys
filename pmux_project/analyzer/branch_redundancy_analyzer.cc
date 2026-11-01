#include "pmux_branch_proof_api.h"
#include "guarded_fact_analysis.h"

#include "kernel/sigtools.h"

#include <string>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

bool exact_budget(const AnalysisBudget &budget)
{
	return budget.max_cells == 512 && budget.max_bits == 8192 &&
		budget.max_events == 262144;
}

bool validate_request(const BranchProofRequest &r, std::string &reason)
{
	if (r.module == nullptr || r.cell == nullptr) {
		reason = "invalid_null_object";
		return false;
	}
	if (r.cell->module != r.module || r.module->cell(r.cell->name) != r.cell) {
		reason = "invalid_cell_membership";
		return false;
	}
	if (r.module_id != r.module->name.str() || r.cell_id != r.cell->name.str()) {
		reason = "invalid_stable_id";
		return false;
	}
	if (!exact_budget(r.budget)) {
		reason = "invalid_budget";
		return false;
	}
	if (r.cell->type != ID($pmux) || !r.cell->hasParam(ID::WIDTH) ||
			!r.cell->hasParam(ID::S_WIDTH) || !r.cell->hasPort(ID::A) ||
			!r.cell->hasPort(ID::B) || !r.cell->hasPort(ID::S) ||
			!r.cell->hasPort(ID::Y)) {
		reason = "invalid_pmux_shape";
		return false;
	}
	const int width = r.cell->getParam(ID::WIDTH).as_int();
	const int s_width = r.cell->getParam(ID::S_WIDTH).as_int();
	if (width <= 0 || s_width <= 0 || r.branch_index < 0 ||
			r.branch_index >= s_width || GetSize(r.cell->getPort(ID::A)) != width ||
			GetSize(r.cell->getPort(ID::B)) != width * s_width ||
			GetSize(r.cell->getPort(ID::S)) != s_width ||
			GetSize(r.cell->getPort(ID::Y)) != width) {
		reason = "invalid_pmux_shape";
		return false;
	}
	if (r.a_snapshot != r.cell->getPort(ID::A) ||
			r.b_snapshot != r.cell->getPort(ID::B) ||
			r.s_snapshot != r.cell->getPort(ID::S) ||
			r.y_snapshot != r.cell->getPort(ID::Y)) {
		reason = "invalid_snapshot";
		return false;
	}
	return true;
}

const BitConclusion *find_bit(const EntryAnalysisResult &result, int index)
{
	for (const BitConclusion &bit : result.bits)
		if (bit.bit_index == index)
			return &bit;
	return nullptr;
}

struct ObservedFact {
	FactValue value = FactValue::Unknown;
	int proof_id = -1;
	std::string reason = "fixed_point_unknown";
};

ObservedFact observe(const RTLIL::SigBit &original,
		const EntryAnalysisResult &result, int index)
{
	if (original.wire == nullptr) {
		if (original.data == RTLIL::State::S0)
			return {FactValue::Zero, -1, "constant"};
		if (original.data == RTLIL::State::S1)
			return {FactValue::One, -1, "constant"};
		return {FactValue::Unknown, -1, "original_xz"};
	}
	const BitConclusion *bit = find_bit(result, index);
	if (bit == nullptr)
		return {};
	return {bit->value, bit->proof_id, bit->reason_code};
}

class ReadonlyBranchRedundancyAnalyzer final : public BranchRedundancyAnalyzer
{
public:
	BranchProof prove(const BranchProofRequest &request) const override
	{
		BranchProof proof;
		proof.cell = request.cell_id;
		proof.branch_index = request.branch_index;

		std::string validation_error;
		if (!validate_request(request, validation_error)) {
			proof.status = EntryStatus::InvalidRequest;
			proof.reason_code = validation_error;
			return proof;
		}

		const int width = request.cell->getParam(ID::WIDTH).as_int();
		const int s_width = request.cell->getParam(ID::S_WIDTH).as_int();
		const RTLIL::SigSpec a = request.cell->getPort(ID::A);
		const RTLIL::SigSpec b = request.cell->getPort(ID::B);
		const RTLIL::SigSpec s = request.cell->getPort(ID::S);
		const RTLIL::SigSpec branch = b.extract(request.branch_index * width, width);
		proof.width = width;

		// RTLIL $pmux has one-hot, not priority, semantics.  The branch is the
		// unique selected value only on this guard.  We prove the zero facts from
		// S_i=1 below; they are not silently assumed as mutual exclusion.
		proof.effective_guard.push_back({s[request.branch_index], RTLIL::State::S1});
		for (int i = 0; i < s_width; ++i)
			if (i != request.branch_index)
				proof.effective_guard.push_back({s[i], RTLIL::State::S0});

		RTLIL::SigSpec targets = s;
		targets.append(branch);
		targets.append(a);
		std::vector<GuardFact> premise_guard = {
			{s[request.branch_index], RTLIL::State::S1}
		};
		EntryAnalysisResult facts = analyze_guarded_values(request.module, targets,
				premise_guard, request.budget);
		proof.status = facts.status;
		proof.usage = facts.usage;
		proof.proof_records = facts.proof_records;

		auto fill_all = [&](BranchBitStatus status, const std::string &reason) {
			for (int bit = 0; bit < width; ++bit)
				proof.per_bit_status.push_back({bit, status, FactValue::Unknown,
						FactValue::Unknown, -1, -1, reason});
		};
		if (facts.status == EntryStatus::FactConflict) {
			proof.reason_code = "fact_conflict";
			fill_all(BranchBitStatus::FactConflict, proof.reason_code);
			return proof;
		}
		if (facts.status == EntryStatus::BudgetExhausted) {
			proof.reason_code = facts.reason_code;
			fill_all(BranchBitStatus::BudgetExhausted, proof.reason_code);
			return proof;
		}

		// Exact $pmux equivalence requires proving S_i excludes every other
		// selector.  A reachable overlap is undefined in the source cell, while
		// deleting i could make the reduced cell defined.
		for (int i = 0; i < s_width; ++i) {
			if (i == request.branch_index)
				continue;
			ObservedFact selected = observe(s[i], facts, i);
			if (selected.value == FactValue::One) {
				proof.reason_code = "selection_overlap_reachable";
				fill_all(BranchBitStatus::Unknown, proof.reason_code);
				return proof;
			}
			if (selected.value != FactValue::Zero) {
				proof.reason_code = selected.reason == "original_xz" ?
						"selection_xz" : "selection_overlap_unknown";
				fill_all(BranchBitStatus::Unknown, proof.reason_code);
				return proof;
			}
		}

		// Once one-hotness under S_i is proved, deletion leaves no active branch;
		// therefore the exact fallback is A.  Lower-priority fallback is not a
		// legal concept for a native RTLIL $pmux.
		proof.fallback_kind = BranchFallbackKind::DefaultA;
		proof.fallback_branch_index = -1;
		SigMap sigmap(const_cast<RTLIL::Module *>(request.module));
		bool all_equal = true;
		for (int bit = 0; bit < width; ++bit) {
			BranchBitProof result;
			result.bit_index = bit;
			ObservedFact lhs = observe(branch[bit], facts, s_width + bit);
			ObservedFact rhs = observe(a[bit], facts, s_width + width + bit);
			result.branch_value = lhs.value;
			result.fallback_value = rhs.value;
			result.branch_proof_id = lhs.proof_id;
			result.fallback_proof_id = rhs.proof_id;
			if (sigmap(branch[bit]) == sigmap(a[bit])) {
				result.status = BranchBitStatus::ProvenEqual;
				result.reason_code = "structural_alias";
			} else if (lhs.value != FactValue::Unknown && lhs.value == rhs.value) {
				result.status = BranchBitStatus::ProvenEqual;
				result.reason_code = "equal_binary_facts";
			} else {
				result.status = BranchBitStatus::Unknown;
				result.reason_code = "bit_equality_unknown";
				all_equal = false;
			}
			proof.per_bit_status.push_back(result);
		}
		proof.proven_all_bits = all_equal;
		proof.reason_code = all_equal ? "branch_redundant" : "partial_or_unknown_equality";
		return proof;
	}
};

} // namespace

std::unique_ptr<BranchRedundancyAnalyzer> make_branch_redundancy_analyzer()
{
	return std::make_unique<ReadonlyBranchRedundancyAnalyzer>();
}

} // namespace PmuxOpt

YOSYS_NAMESPACE_END
