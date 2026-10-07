#include "pmux_v02_case34_proof_api.h"
#include "budget_tracker.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

int case34_checks = 0;

void c34_require(bool condition, const std::string &message)
{
	++case34_checks;
	if (!condition) log_error("PMUX case34 selftest failed: %s\n", message.c_str());
}

RTLIL::IdString cid(const std::string &name) { return RTLIL::escape_id(name); }

CellSnapshot take_snapshot(const RTLIL::Cell *cell)
{
	CellSnapshot out; out.cell_id = cell->name.str(); out.type = cell->type;
	for (const auto &conn : cell->connections()) out.ports.push_back({conn.first, conn.second});
	for (const auto &param : cell->parameters) out.parameters.push_back({param.first, param.second});
	return out;
}

struct Case34Fixture {
	RTLIL::Design design;
	RTLIL::Module *module;
	int serial = 0;

	Case34Fixture() : module(design.addModule(cid("top"))) { }

	RTLIL::Wire *wire(const std::string &name, int width = 1, bool input = false)
	{
		RTLIL::Wire *w = module->addWire(cid(name + std::to_string(serial++)), width);
		w->port_input = input;
		return w;
	}

	RTLIL::Cell *mux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b, RTLIL::SigBit s)
	{
		RTLIL::Cell *cell = module->addCell(cid("mux" + std::to_string(serial++)), ID($mux));
		RTLIL::Wire *y = wire("my", GetSize(a));
		cell->setParam(ID::WIDTH, GetSize(a)); cell->setPort(ID::A, a); cell->setPort(ID::B, b);
		cell->setPort(ID::S, s); cell->setPort(ID::Y, y); return cell;
	}

	RTLIL::Cell *eq(const RTLIL::SigSpec &a, const RTLIL::Const &b)
	{
		RTLIL::Cell *cell = module->addCell(cid("eq" + std::to_string(serial++)), ID($eq));
		RTLIL::Wire *y = wire("ey");
		cell->setParam(ID::A_WIDTH, GetSize(a)); cell->setParam(ID::B_WIDTH, GetSize(b));
		cell->setParam(ID::A_SIGNED, 0); cell->setParam(ID::B_SIGNED, 0); cell->setParam(ID::Y_WIDTH, 1);
		cell->setPort(ID::A, a); cell->setPort(ID::B, b); cell->setPort(ID::Y, y); return cell;
	}

	RTLIL::Cell *dff(const RTLIL::SigSpec &d)
	{
		RTLIL::Cell *cell = module->addCell(cid("ff" + std::to_string(serial++)), ID($dff));
		RTLIL::Wire *q = wire("q", GetSize(d)); RTLIL::Wire *clk = wire("clk", 1, true);
		cell->setParam(ID::WIDTH, GetSize(d)); cell->setParam(ID::CLK_POLARITY, 1);
		cell->setPort(ID::D, d); cell->setPort(ID::Q, q); cell->setPort(ID::CLK, clk); return cell;
	}

	RTLIL::Cell *dffe(const RTLIL::SigSpec &d)
	{
		RTLIL::Cell *cell = module->addCell(cid("ff" + std::to_string(serial++)), ID($dffe));
		RTLIL::Wire *q = wire("q", GetSize(d)); RTLIL::Wire *clk = wire("clk", 1, true);
		RTLIL::Wire *en = wire("en", 1, true);
		cell->setParam(ID::WIDTH, GetSize(d)); cell->setParam(ID::CLK_POLARITY, 1); cell->setParam(ID::EN_POLARITY, 1);
		cell->setPort(ID::D, d); cell->setPort(ID::Q, q); cell->setPort(ID::CLK, clk); cell->setPort(ID::EN, en); return cell;
	}

	RTLIL::Cell *adffe(const RTLIL::SigSpec &d)
	{
		RTLIL::Cell *cell = module->addCell(cid("ff" + std::to_string(serial++)), ID($adffe));
		RTLIL::Wire *q = wire("q", GetSize(d)); RTLIL::Wire *clk = wire("clk", 1, true);
		RTLIL::Wire *en = wire("en", 1, true); RTLIL::Wire *arst = wire("arst", 1, true);
		cell->setParam(ID::WIDTH, GetSize(d)); cell->setParam(ID::CLK_POLARITY, 1); cell->setParam(ID::EN_POLARITY, 1);
		cell->setParam(ID::ARST_POLARITY, 1); cell->setParam(ID::ARST_VALUE, RTLIL::Const(0, GetSize(d)));
		cell->setPort(ID::D, d); cell->setPort(ID::Q, q); cell->setPort(ID::CLK, clk);
		cell->setPort(ID::EN, en); cell->setPort(ID::ARST, arst); return cell;
	}

	std::string fingerprint()
	{
		module->fixup_ports(); std::ostringstream out;
		RTLIL_BACKEND::dump_module(out, "", module, &design, false); return out.str();
	}
};

struct GuardFixture : Case34Fixture {
	RTLIL::Wire *source;
	RTLIL::Wire *outer_guard;
	RTLIL::Wire *preserved;
	RTLIL::Cell *cmp;
	RTLIL::Cell *inner;
	RTLIL::Cell *outer;
	GuardedConeProofRequest request;

	GuardFixture()
	{
		source = wire("value", 4, true); outer_guard = wire("guard", 1, true); preserved = wire("free", 1, true);
		cmp = eq(source, RTLIL::Const(0xd, 4));
		inner = mux(RTLIL::State::S0, cmp->getPort(ID::Y), RTLIL::SigBit(source, 3));
		outer = mux(RTLIL::State::S0, inner->getPort(ID::Y), outer_guard);
		request.module_id = module->name.str(); request.module = module;
		request.guard_sink_cell_id = outer->name.str(); request.guard_sink_cell = outer;
		request.guard_facts = {{outer_guard, RTLIL::State::S1}, {RTLIL::SigBit(source, 3), RTLIL::State::S1}};
		request.source_value = source; request.source_width = 4; request.removed_bit_indices = {3};
		ComparatorNarrowingRequest cr;
		cr.cell_id = cmp->name.str(); cr.cell = cmp; cr.source_snapshot = source;
		cr.constant_snapshot = RTLIL::Const(0xd, 4); cr.removed_bit_indices = {3};
		cr.full_fanout_snapshot.push_back({inner->name.str(), ID::B, 0, cmp->getPort(ID::Y)[0]});
		request.comparators.push_back(cr);
		request.folded_data_bits.push_back({0, RTLIL::SigBit(source, 0), RTLIL::State::S1, 0});
		request.preserved_variable_bits.push_back({1, preserved});
		request.affected_cells = {outer->name.str(), inner->name.str(), cmp->name.str()};
	}

	GuardedConeProof run()
	{
		std::string before = fingerprint();
		GuardedConeProof out = make_guarded_cone_analyzer()->prove(request);
		c34_require(before == fingerprint(), "guarded proof changed RTLIL fingerprint"); return out;
	}
};

void test_guarded_cone()
{
	{
		GuardFixture f; GuardedConeProof p = f.run();
		c34_require(p.status == V02ProofStatus::Normal && p.proven_all_obligations && p.reason_code == V02ReasonCode::Proved,
				"legal current-cycle guard proves all obligations");
		c34_require(p.comparator_cells.size() == 1 && p.comparator_cells[0].equivalent_under_guard &&
				!p.comparator_cells[0].comparator_must_be_preserved, "comparator narrowed with covered fanout");
		c34_require(p.folded_data_bits[0].status == GuardedDataBitStatus::FoldedOne &&
				p.preserved_variable_bits[0].status == GuardedDataBitStatus::PreservedVariable, "fold and free bit recorded");
	}
	{
		GuardFixture f; RTLIL::Wire *unrelated = f.wire("next_cycle", 1, true);
		f.request.guard_facts[1].bit = unrelated;
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.reason_code == V02ReasonCode::GuardNotDominating,
				"non-dominating or next-cycle-only guard rejected");
	}
	{
		GuardFixture f; RTLIL::Wire *extra = f.wire("extra");
		RTLIL::Cell *fanout = f.mux(RTLIL::State::S0, RTLIL::State::S1, f.cmp->getPort(ID::Y)[0]);
		fanout->setPort(ID::Y, extra);
		f.request.comparators[0].full_fanout_snapshot.push_back({fanout->name.str(), ID::S, 0, f.cmp->getPort(ID::Y)[0]});
		GuardedConeProof p = f.run();
		c34_require(p.proven_all_obligations && p.comparator_cells[0].comparator_must_be_preserved,
				"extra comparator fanout forces preservation");
	}
	{
		GuardFixture f; f.request.guard_facts[1].value = RTLIL::State::S0;
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations, "removed compare bit not implied is rejected");
	}
	{
		GuardFixture f; f.request.folded_data_bits[0] = {0, RTLIL::SigBit(f.source, 1), RTLIL::State::S1, -1};
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.reason_code == V02ReasonCode::PartialDataFold,
				"partial data fold is diagnostic only");
	}
	{
		GuardFixture f; f.request.preserved_variable_bits[0].result_bit_index = 0;
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.reason_code == V02ReasonCode::PreservedBitMissing,
				"missing free bit preservation rejects whole proof");
	}
	{
		GuardFixture f; f.cmp->setParam(ID::A_SIGNED, 1);
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.comparator_cells[0].reason_code == V02ReasonCode::ComparisonSignednessUnknown,
				"signedness mismatch rejected");
	}
	{
		GuardFixture f; f.request.comparators[0].full_fanout_snapshot.clear();
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.comparator_cells[0].reason_code == V02ReasonCode::SnapshotMismatch,
				"fanout snapshot mismatch rejected");
	}
	{
		GuardFixture f; f.request.guard_facts[0].value = RTLIL::State::Sx;
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.reason_code == V02ReasonCode::GuardXz,
				"x/z guard rejected explicitly");
	}
	{
		GuardFixture f;
		f.cmp->setPort(ID::B, RTLIL::Const(std::vector<RTLIL::State>{RTLIL::State::S1, RTLIL::State::S0,
				RTLIL::State::S1, RTLIL::State::Sx}));
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations, "x/z comparator constant rejected");
	}
	{
		GuardFixture f; f.module->connect(f.outer_guard, RTLIL::State::S0);
		GuardedConeProof p = f.run();
		c34_require(!p.proven_all_obligations && p.status == V02ProofStatus::FactConflict,
				"guard fact conflict rejects whole proof");
	}
}

struct TableFixture : Case34Fixture {
	RTLIL::Wire *address;
	RTLIL::SigSpec root;
	std::vector<std::string> cells;
	RTLIL::Cell *sink;
	ConstantTableProofRequest request;

	RTLIL::SigSpec build(int level, int base)
	{
		if (level == 0) return RTLIL::Const((base * 0x25u + 0x270u) & 0x3ffu, 10);
		RTLIL::SigSpec a = build(level - 1, base);
		RTLIL::SigSpec b = build(level - 1, base + (1 << (level - 1)));
		RTLIL::Cell *cell = mux(a, b, RTLIL::SigBit(address, level - 1));
		cells.push_back(cell->name.str()); return cell->getPort(ID::Y);
	}

	explicit TableFixture(int sink_kind = 0)
	{
		address = wire("address", 5, true); root = build(5, 0);
		sink = sink_kind == 1 ? dffe(root) : sink_kind == 2 ? adffe(root) : dff(root);
		request.module_id = module->name.str(); request.module = module; request.root_outputs = root;
		for (int i = 0; i < 5; ++i) request.address_bits_lsb_first.push_back(RTLIL::SigBit(address, i));
		for (int i = 0; i < 10; ++i) request.output_bits_lsb_first.push_back(root[i]);
		request.source_cells = cells;
		SequentialSinkRequest sr; sr.cell_id = sink->name.str(); sr.cell = sink; sr.snapshot = take_snapshot(sink);
		for (int i = 0; i < 10; ++i) sr.root_output_bit_indices.push_back(i);
		request.sequential_sinks.push_back(sr);
	}

	ConstantTableProof run()
	{
		std::string before = fingerprint(); ConstantTableProof out = make_constant_table_analyzer()->prove(request);
		c34_require(before == fingerprint(), "table proof changed RTLIL fingerprint"); return out;
	}
};

void test_constant_table()
{
	{
		TableFixture f; ConstantTableProof p = f.run();
		c34_require(p.status == V02ProofStatus::Normal && p.complete_binary_domain && p.proven_all_rows && p.truth_table.size() == 32,
				"complete 5-to-10 table proves");
		c34_require(p.truth_table[0].as_int() == 0x270 && p.address_bits_lsb_first.size() == 5 &&
				p.output_bits_lsb_first.size() == 10, "row encoding and bit orders recorded");
		c34_require(p.reset_enable_hold_contract.reset_preserved && p.reset_enable_hold_contract.enable_preserved &&
				p.reset_enable_hold_contract.hold_preserved, "sequential contract snapshotted");
	}
	{
		TableFixture f; std::reverse(f.request.address_bits_lsb_first.begin(), f.request.address_bits_lsb_first.end());
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::AddressOrderUnknown,
				"reversed address order rejected");
	}
	{
		TableFixture f; std::reverse(f.request.output_bits_lsb_first.begin(), f.request.output_bits_lsb_first.end());
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::OutputOrderUnknown,
				"reversed output order rejected");
	}
	{
		TableFixture f; f.request.address_bits_lsb_first.pop_back();
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::TableIncomplete,
				"missing address dimension rejected");
	}
	{
		TableFixture f; f.request.address_bits_lsb_first[4] = f.request.address_bits_lsb_first[3];
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::AddressOrderUnknown,
				"duplicate conflicting address identity rejected");
	}
	{
		TableFixture f; f.request.source_cells.pop_back();
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::SnapshotMismatch,
				"incomplete source-cell set rejected");
	}
	{
		TableFixture f;
		for (RTLIL::Cell *cell : f.module->cells())
			if (cell->type == ID($mux) && cell->getPort(ID::A).is_fully_const()) {
				RTLIL::Const original = cell->getPort(ID::A).as_const();
				std::vector<RTLIL::State> states;
				for (int i = 0; i < GetSize(original); ++i) states.push_back(i == 0 ? RTLIL::State::Sx : original[i]);
				RTLIL::Const bad(states);
				cell->setPort(ID::A, bad); break;
			}
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::TableXz,
				"x/z table entry rejected");
	}
	{
		TableFixture f; RTLIL::Cell *second = f.module->addCell(cid("second_driver"), ID($not));
		second->setParam(ID::A_WIDTH, 1); second->setParam(ID::A_SIGNED, 0); second->setParam(ID::Y_WIDTH, 1);
		second->setPort(ID::A, RTLIL::State::S0); second->setPort(ID::Y, f.root[0]);
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::MultipleDriver,
				"multiple-driver table root rejected");
	}
	{
		TableFixture f;
		for (RTLIL::Cell *cell : f.module->cells())
			if (cell->type == ID($mux) && cell->getPort(ID::A).is_fully_const()) {
				cell->setPort(ID::A, f.wire("missing_leaf", 10, true)); break;
			}
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::TableIncomplete,
				"incomplete selection cone rejected");
	}
	{
		TableFixture f(2); bool found = false;
		for (CellParameterSnapshot &param : f.request.sequential_sinks[0].snapshot.parameters)
			if (param.parameter == ID::ARST_VALUE) { param.value = RTLIL::Const(1, 10); found = true; }
		c34_require(found, "reset parameter present in snapshot");
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::SequentialBoundaryChanged,
				"reset snapshot mismatch rejected");
	}
	{
		TableFixture f(1); bool found = false;
		for (CellParameterSnapshot &param : f.request.sequential_sinks[0].snapshot.parameters)
			if (param.parameter == ID::EN_POLARITY) { param.value = RTLIL::Const(0, 1); found = true; }
		c34_require(found, "enable parameter present in snapshot");
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::SequentialBoundaryChanged,
				"enable snapshot mismatch rejected");
	}
	{
		TableFixture f; f.request.sequential_sinks.clear();
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::ResetEnableHoldUnknown,
				"unknown hold contract rejected");
	}
	{
		TableFixture f; f.request.sequential_sinks[0].root_output_bit_indices[0] = 10;
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::SequentialBoundaryChanged,
				"sequential sink out of range rejected");
	}
	{
		TableFixture f; f.dff(f.root);
		ConstantTableProof p = f.run(); c34_require(!p.proven_all_rows && p.reason_code == V02ReasonCode::ResetEnableHoldUnknown,
				"unlisted sequential sink rejected");
	}
	for (PmuxOptTestable::BudgetKind kind : {PmuxOptTestable::BudgetKind::Cells,
			PmuxOptTestable::BudgetKind::Bits, PmuxOptTestable::BudgetKind::Events}) {
		PmuxOptTestable::BudgetTracker tracker(1, 1, 1);
		bool first = kind == PmuxOptTestable::BudgetKind::Cells ? tracker.take_cell() :
			kind == PmuxOptTestable::BudgetKind::Bits ? tracker.take_bit() : tracker.take_event();
		bool second = kind == PmuxOptTestable::BudgetKind::Cells ? tracker.take_cell() :
			kind == PmuxOptTestable::BudgetKind::Bits ? tracker.take_bit() : tracker.take_event();
		c34_require(first && !second && tracker.exhausted() == kind, "shared budget boundary is fail closed");
	}
}

void run_case34_tests()
{
	test_guarded_cone(); test_constant_table();
	c34_require(std::string(v02_reason_code_name(V02ReasonCode::SatUnknown)) == "sat_unknown", "reason codes are stable strings");
	log("PMUX case34 selftest: all checks passed (%d assertions)\n", case34_checks);
}

struct Case34SelftestRunner { Case34SelftestRunner() { run_case34_tests(); } } case34_selftest_runner;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
