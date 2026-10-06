#include "pmux_branch_proof_api.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"
#include "kernel/register.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

int branch_checks = 0;

void branch_require(bool condition, const std::string &message)
{
	++branch_checks;
	if (!condition)
		log_error("PMUX branch selftest failed: %s\n", message.c_str());
}

RTLIL::IdString bid(const std::string &name)
{
	return RTLIL::escape_id(name);
}

struct BranchFixture {
	RTLIL::Design design;
	RTLIL::Module *module;
	int serial = 0;

	BranchFixture() : module(design.addModule(bid("top"))) { }

	RTLIL::Wire *wire(const std::string &stem, int width = 1, bool input = false)
	{
		RTLIL::Wire *w = module->addWire(bid(stem + std::to_string(serial++)), width);
		w->port_input = input;
		return w;
	}

	RTLIL::Cell *not_cell(RTLIL::SigBit a, RTLIL::SigBit y)
	{
		RTLIL::Cell *c = module->addCell(bid("not" + std::to_string(serial++)), ID($not));
		c->setParam(ID::A_WIDTH, 1);
		c->setParam(ID::A_SIGNED, 0);
		c->setParam(ID::Y_WIDTH, 1);
		c->setParam(ID::WIDTH, 1);
		c->setPort(ID::A, a);
		c->setPort(ID::Y, y);
		return c;
	}

	RTLIL::Cell *eq(const RTLIL::SigSpec &a, const RTLIL::Const &b)
	{
		RTLIL::Wire *y = wire("eq_y");
		RTLIL::Cell *c = module->addCell(bid("eq" + std::to_string(serial++)), ID($eq));
		c->setParam(ID::A_WIDTH, GetSize(a));
		c->setParam(ID::B_WIDTH, GetSize(b));
		c->setParam(ID::Y_WIDTH, 1);
		c->setParam(ID::A_SIGNED, 0);
		c->setParam(ID::B_SIGNED, 0);
		c->setPort(ID::A, a);
		c->setPort(ID::B, b);
		c->setPort(ID::Y, y);
		return c;
	}

	RTLIL::Cell *pmux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b,
			const RTLIL::SigSpec &s)
	{
		branch_require(GetSize(b) == GetSize(a) * GetSize(s), "fixture pmux widths");
		RTLIL::Wire *y = wire("pmux_y", GetSize(a));
		RTLIL::Cell *c = module->addCell(bid("pmux" + std::to_string(serial++)), ID($pmux));
		c->setParam(ID::WIDTH, GetSize(a));
		c->setParam(ID::S_WIDTH, GetSize(s));
		c->setPort(ID::A, a);
		c->setPort(ID::B, b);
		c->setPort(ID::S, s);
		c->setPort(ID::Y, y);
		return c;
	}

	BranchProofRequest request(RTLIL::Cell *cell, int branch)
	{
		BranchProofRequest r;
		r.module_id = module->name.str();
		r.cell_id = cell->name.str();
		r.module = module;
		r.cell = cell;
		r.branch_index = branch;
		r.a_snapshot = cell->getPort(ID::A);
		r.b_snapshot = cell->getPort(ID::B);
		r.s_snapshot = cell->getPort(ID::S);
		r.y_snapshot = cell->getPort(ID::Y);
		return r;
	}

	std::string fingerprint()
	{
		module->fixup_ports();
		std::ostringstream out;
		RTLIL_BACKEND::dump_module(out, "", module, &design, false);
		return out.str();
	}

	BranchProof run(RTLIL::Cell *cell, int branch)
	{
		std::string before = fingerprint();
		auto analyzer = make_branch_redundancy_analyzer();
		BranchProof result = analyzer->prove(request(cell, branch));
		branch_require(before == fingerprint(), "branch proof changed RTLIL fingerprint");
		return result;
	}
};

void test_equal_a_and_partial_unknown()
{
	{
		BranchFixture f;
		auto *a = f.wire("a", 2, true);
		auto *s = f.wire("s", 1, true);
		auto result = f.run(f.pmux(a, a, s), 0);
		branch_require(result.proven_all_bits && result.reason_code == "branch_redundant",
				"whole branch equal to A");
		branch_require(result.fallback_kind == BranchFallbackKind::DefaultA,
				"native pmux fallback is A");
		branch_require(result.per_bit_status.size() == 2 &&
				result.per_bit_status[0].status == BranchBitStatus::ProvenEqual &&
				result.per_bit_status[1].status == BranchBitStatus::ProvenEqual,
				"all alias bits proven");
	}
	{
		BranchFixture f;
		auto *same = f.wire("same", 1, true);
		auto *a1 = f.wire("a1", 1, true);
		auto *b1 = f.wire("b1", 1, true);
		auto *s = f.wire("s", 1, true);
		RTLIL::SigSpec a(same); a.append(a1);
		RTLIL::SigSpec b(same); b.append(b1);
		auto result = f.run(f.pmux(a, b, s), 0);
		branch_require(!result.proven_all_bits && result.reason_code == "partial_or_unknown_equality",
				"partial equality cannot authorize deletion");
		branch_require(result.per_bit_status[0].status == BranchBitStatus::ProvenEqual &&
				result.per_bit_status[1].status == BranchBitStatus::Unknown,
				"partial equality remains per-bit diagnostic only");
	}
	{
		BranchFixture f;
		auto *a = f.wire("a", 1, true);
		auto *b = f.wire("b", 1, true);
		auto *s = f.wire("s", 1, true);
		auto result = f.run(f.pmux(a, b, s), 0);
		branch_require(!result.proven_all_bits && result.per_bit_status[0].status == BranchBitStatus::Unknown,
				"unknown equality rejected");
	}
}

void test_overlap_and_no_priority_fallback()
{
	{
		BranchFixture f;
		auto *a = f.wire("a", 1, true);
		auto *b0 = f.wire("b0", 1, true);
		auto *s0 = f.wire("s0", 1, true);
		auto *s1 = f.wire("s1", 1, true);
		RTLIL::SigSpec b(b0); b.append(a);
		RTLIL::SigSpec s(s0); s.append(s1);
		auto result = f.run(f.pmux(a, b, s), 1);
		branch_require(!result.proven_all_bits && result.reason_code == "selection_overlap_unknown",
				"independent overlap is not assumed impossible");
	}
	{
		BranchFixture f;
		auto *a = f.wire("a", 1, true);
		auto *lower = f.wire("lower", 1, true);
		auto *shared_s = f.wire("shared_s", 1, true);
		RTLIL::SigSpec b(lower); b.append(lower);
		RTLIL::SigSpec s(shared_s); s.append(shared_s);
		auto result = f.run(f.pmux(a, b, s), 1);
		branch_require(!result.proven_all_bits && result.reason_code == "selection_overlap_reachable",
				"equal lower branch does not invent pmux priority semantics");
		branch_require(result.fallback_kind == BranchFallbackKind::Unresolved,
				"overlap has no lower-priority fallback in native pmux");
	}
}

void test_budget_and_conflict()
{
	{
		BranchFixture f;
		auto *s = f.wire("s", 1, true);
		RTLIL::SigBit current = s;
		for (int i = 0; i < 513; ++i) {
			auto *next = f.wire("chain");
			f.not_cell(current, next);
			current = next;
		}
		auto result = f.run(f.pmux(RTLIL::SigSpec(current), RTLIL::SigSpec(current), s), 0);
		branch_require(!result.proven_all_bits && result.status == EntryStatus::BudgetExhausted &&
				result.reason_code == "budget_cells_exhausted", "cell budget exhaustion is conservative");
	}
	{
		BranchFixture f;
		auto *a = f.wire("a", 1, true);
		auto result = f.run(f.pmux(a, a, RTLIL::SigSpec(RTLIL::State::S0)), 0);
		branch_require(!result.proven_all_bits && result.status == EntryStatus::FactConflict &&
				result.reason_code == "fact_conflict", "contradictory branch guard conflicts");
	}
}

void test_case3_v2b_shape()
{
	BranchFixture f;
	auto *sr = f.wire("sr", 8, true);
	RTLIL::Cell *cmp_ff = f.eq(sr, RTLIL::Const(0xff, 8));
	RTLIL::Cell *cmp_aa = f.eq(sr, RTLIL::Const(0xaa, 8));
	RTLIL::SigSpec sr_sig(sr);
	RTLIL::SigSpec branches(sr_sig[3]);
	branches.append(RTLIL::State::S1);
	RTLIL::SigSpec selects(cmp_ff->getPort(ID::Y));
	selects.append(cmp_aa->getPort(ID::Y));
	auto result = f.run(f.pmux(RTLIL::SigSpec(sr_sig[0]), branches, selects), 0);
	branch_require(result.proven_all_bits && result.fallback_kind == BranchFallbackKind::DefaultA &&
			result.reason_code == "branch_redundant", "case3 V2-B comparator shape proves branch 0");
	branch_require(result.per_bit_status.size() == 1 &&
			result.per_bit_status[0].reason_code == "equal_binary_facts" &&
			result.per_bit_status[0].branch_value == FactValue::One &&
			result.per_bit_status[0].fallback_value == FactValue::One,
			"case3 V2-B proof uses equal binary facts");
}

void run_branch_tests()
{
	test_equal_a_and_partial_unknown();
	test_overlap_and_no_priority_fallback();
	test_budget_and_conflict();
	test_case3_v2b_shape();
	log("PMUX branch selftest: all checks passed (%d assertions)\n", branch_checks);
}

struct BranchSelftestRunner {
	BranchSelftestRunner() { run_branch_tests(); }
} branch_selftest_runner;

std::string design_fingerprint(RTLIL::Design *design)
{
	std::ostringstream out;
	std::vector<RTLIL::Module *> modules = design->modules();
	std::sort(modules.begin(), modules.end(), [](RTLIL::Module *a, RTLIL::Module *b) {
		return a->name.str() < b->name.str();
	});
	for (RTLIL::Module *module : modules)
		RTLIL_BACKEND::dump_module(out, "", module, design, false);
	return out.str();
}

struct PmuxBranchProofReportPass : public Pass {
	PmuxBranchProofReportPass() : Pass("pmux_branch_proof_report",
			"emit read-only whole-$pmux-branch proof diagnostics") { }

	void help() override
	{
		log("    pmux_branch_proof_report\n");
		log("\nEmit one deterministic record for every branch of every selected $pmux.\n");
		log("The command does not modify the design.\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);
		const std::string before = design_fingerprint(design);
		auto analyzer = make_branch_redundancy_analyzer();
		for (RTLIL::Module *module : design->selected_modules()) {
			std::vector<RTLIL::Cell *> cells = module->selected_cells();
			std::sort(cells.begin(), cells.end(), [](RTLIL::Cell *a, RTLIL::Cell *b) {
				return a->name.str() < b->name.str();
			});
			for (RTLIL::Cell *cell : cells) {
				if (cell->type != ID($pmux) || !cell->hasParam(ID::S_WIDTH))
					continue;
				for (int branch = 0; branch < cell->getParam(ID::S_WIDTH).as_int(); ++branch) {
					BranchProofRequest request;
					request.module_id = module->name.str();
					request.cell_id = cell->name.str();
					request.module = module;
					request.cell = cell;
					request.branch_index = branch;
					request.a_snapshot = cell->getPort(ID::A);
					request.b_snapshot = cell->getPort(ID::B);
					request.s_snapshot = cell->getPort(ID::S);
					request.y_snapshot = cell->getPort(ID::Y);
					BranchProof proof = analyzer->prove(request);
					log("BRANCH_PROOF module=%s cell=%s branch=%d width=%d proven_all_bits=%d fallback=%s reason=%s cells=%zu bits=%zu events=%zu\n",
							log_id(module), log_id(cell), branch, proof.width, proof.proven_all_bits,
							proof.fallback_kind == BranchFallbackKind::DefaultA ? "A" :
							proof.fallback_kind == BranchFallbackKind::LowerPriorityBranch ? "lower" : "unresolved",
							proof.reason_code.c_str(), proof.usage.visited_cells,
							proof.usage.visited_bits, proof.usage.propagation_events);
					for (const BranchBitProof &bit : proof.per_bit_status)
						log("BRANCH_PROOF_BIT cell=%s branch=%d bit=%d status=%d branch_value=%d fallback_value=%d branch_proof_id=%d fallback_proof_id=%d reason=%s\n",
								log_id(cell), branch, bit.bit_index, int(bit.status), int(bit.branch_value),
								int(bit.fallback_value), bit.branch_proof_id, bit.fallback_proof_id,
								bit.reason_code.c_str());
					if (proof.proven_all_bits)
						for (const ProofRecord &record : proof.proof_records) {
							std::ostringstream parents;
							for (std::size_t i = 0; i < record.parent_proof_ids.size(); ++i) {
								if (i)
									parents << ',';
								parents << record.parent_proof_ids[i];
							}
							log("BRANCH_PROOF_TRACE cell=%s branch=%d proof_id=%d target=%s value=%d rule=%s source=%s parents=%s\n",
									log_id(cell), branch, record.proof_id,
									log_signal(RTLIL::SigSpec(record.target)), int(record.value),
									record.rule.c_str(), record.source_cell_id.c_str(),
									parents.str().c_str());
						}
				}
			}
		}
		log_assert(before == design_fingerprint(design));
		log("BRANCH_PROOF_NETLIST_UNCHANGED 1\n");
	}
} PmuxBranchProofReportPass;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
