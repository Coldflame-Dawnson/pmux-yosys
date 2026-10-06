#include "readonly_analyzer.h"
#include "budget_tracker.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"

#include <functional>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

using PmuxOptTestable::BudgetKind;
using PmuxOptTestable::BudgetTracker;

int checks = 0;

void require(bool condition, const std::string &message)
{
	++checks;
	if (!condition)
		log_error("PMUX analyzer selftest failed: %s\n", message.c_str());
}

RTLIL::IdString eid(const std::string &name)
{
	return RTLIL::escape_id(name);
}

struct Fixture {
	RTLIL::Design design;
	RTLIL::Module *module;
	int serial = 0;

	Fixture() : module(design.addModule(eid("top"))) { }

	RTLIL::Wire *wire(const std::string &stem, int width = 1, bool input = false)
	{
		RTLIL::Wire *w = module->addWire(eid(stem + std::to_string(serial++)), width);
		w->port_input = input;
		return w;
	}

	RTLIL::Cell *cell(RTLIL::IdString type, const RTLIL::SigSpec &a,
			const RTLIL::SigSpec &y, const RTLIL::SigSpec &b = RTLIL::SigSpec())
	{
		RTLIL::Cell *c = module->addCell(eid("c" + std::to_string(serial++)), type);
		c->setPort(ID::A, a);
		if (GetSize(b))
			c->setPort(ID::B, b);
		c->setPort(ID::Y, y);
		c->setParam(ID::A_WIDTH, GetSize(a));
		c->setParam(ID::A_SIGNED, 0);
		if (GetSize(b)) {
			c->setParam(ID::B_WIDTH, GetSize(b));
			c->setParam(ID::B_SIGNED, 0);
		}
		c->setParam(ID::Y_WIDTH, GetSize(y));
		if (type.in(ID($not), ID($and), ID($or)))
			c->setParam(ID::WIDTH, GetSize(y));
		return c;
	}

	RTLIL::Cell *mux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b,
			RTLIL::SigBit s, const std::string &stem = "target")
	{
		require(GetSize(a) == GetSize(b), stem + " mux widths");
		RTLIL::Wire *y = wire(stem + "_y", GetSize(a));
		RTLIL::Cell *c = module->addCell(eid(stem + std::to_string(serial++)), ID($mux));
		c->setParam(ID::WIDTH, GetSize(a));
		c->setPort(ID::A, a);
		c->setPort(ID::B, b);
		c->setPort(ID::S, s);
		c->setPort(ID::Y, y);
		return c;
	}

	RTLIL::Cell *pmux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b,
			const RTLIL::SigSpec &s, const std::string &stem = "target")
	{
		RTLIL::Wire *y = wire(stem + "_y", GetSize(a));
		RTLIL::Cell *c = module->addCell(eid(stem + std::to_string(serial++)), ID($pmux));
		c->setParam(ID::WIDTH, GetSize(a));
		c->setParam(ID::S_WIDTH, GetSize(s));
		c->setPort(ID::A, a);
		c->setPort(ID::B, b);
		c->setPort(ID::S, s);
		c->setPort(ID::Y, y);
		return c;
	}

	EntryRequest mux_request(RTLIL::Cell *target, EntryKind kind)
	{
		EntryRequest r;
		r.module_id = module->name.str();
		r.cell_id = target->name.str();
		r.module = module;
		r.cell = target;
		r.entry_kind = kind;
		bool b = kind == EntryKind::MuxB;
		r.physical_port = b ? PhysicalPort::B : PhysicalPort::A;
		r.width = target->getParam(ID::WIDTH).as_int();
		r.data = target->getPort(b ? ID::B : ID::A);
		r.physical_port_snapshot = r.data;
		r.guard.push_back({target->getPort(ID::S)[0], b ? RTLIL::State::S1 : RTLIL::State::S0});
		return r;
	}

	EntryRequest pmux_request(RTLIL::Cell *target, EntryKind kind, int branch = -1)
	{
		EntryRequest r;
		r.module_id = module->name.str();
		r.cell_id = target->name.str();
		r.module = module;
		r.cell = target;
		r.entry_kind = kind;
		r.width = target->getParam(ID::WIDTH).as_int();
		RTLIL::SigSpec s = target->getPort(ID::S);
		if (kind == EntryKind::PmuxB) {
			r.physical_port = PhysicalPort::B;
			r.branch_index = branch;
			r.physical_offset = branch * r.width;
			r.physical_port_snapshot = target->getPort(ID::B);
			r.data = r.physical_port_snapshot.extract(r.physical_offset, r.width);
			r.guard.push_back({s[branch], RTLIL::State::S1});
		} else {
			r.physical_port = PhysicalPort::A;
			r.physical_port_snapshot = target->getPort(ID::A);
			r.data = r.physical_port_snapshot;
			for (RTLIL::SigBit bit : s)
				r.guard.push_back({bit, RTLIL::State::S0});
		}
		return r;
	}

	std::string fingerprint()
	{
		module->fixup_ports();
		std::ostringstream out;
		RTLIL_BACKEND::dump_module(out, "", module, &design, false);
		return out.str();
	}

	EntryAnalysisResult run(const EntryRequest &r)
	{
		std::string before = fingerprint();
		auto analyzer = make_readonly_entry_analyzer();
		EntryAnalysisResult result = analyzer->analyze(r);
		require(before == fingerprint(), "analysis changed RTLIL fingerprint");
		return result;
	}
};

const BitConclusion &bit(const EntryAnalysisResult &r, int index)
{
	for (const auto &b : r.bits)
		if (b.bit_index == index)
			return b;
	log_error("PMUX analyzer selftest failed: missing bit %d\n", index);
}

void trace_proof(const EntryAnalysisResult &r, int id, std::vector<bool> &visiting);

void expect_value(const EntryAnalysisResult &r, int index, FactValue value, const std::string &name)
{
	const BitConclusion &b = bit(r, index);
	require(b.value == value, name + " value (actual=" + std::to_string(int(b.value)) +
			", status=" + std::to_string(int(r.status)) + ", reason=" + b.reason_code + ")");
	require(b.reason_code == "proved", name + " reason");
	require(b.proof_id >= 0 && b.proof_id < GetSize(r.proof_records), name + " proof id");
	std::vector<bool> visiting(r.proof_records.size(), false);
	trace_proof(r, b.proof_id, visiting);
}

void expect_unknown(const EntryAnalysisResult &r, int index, const std::string &name,
		const std::string &reason = "fixed_point_unknown")
{
	const BitConclusion &b = bit(r, index);
	require(b.value == FactValue::Unknown, name + " remains unknown");
	require(b.proof_id == -1, name + " has no proof");
	require(b.reason_code == reason, name + " reason (actual=" + b.reason_code + ")");
}

void trace_proof(const EntryAnalysisResult &r, int id, std::vector<bool> &visiting)
{
	require(id >= 0 && id < GetSize(r.proof_records), "trace proof id in range");
	require(!visiting[id], "proof graph acyclic");
	visiting[id] = true;
	const ProofRecord &p = r.proof_records[id];
	require(p.proof_id == id, "proof record stable id");
	if (p.parent_proof_ids.empty())
		require(p.rule == "guard" || p.rule == "constant", "proof root is guard or constant");
	for (int parent : p.parent_proof_ids)
		trace_proof(r, parent, visiting);
	visiting[id] = false;
}

std::string digest(const EntryAnalysisResult &r)
{
	std::ostringstream out;
	out << int(r.status) << ':' << r.reason_code << ':' << r.usage.visited_cells << ':'
		<< r.usage.visited_bits << ':' << r.usage.propagation_events;
	for (const auto &b : r.bits)
		out << "|b" << b.bit_index << ',' << int(b.value) << ',' << b.proof_id << ',' << b.reason_code;
	for (const auto &p : r.proof_records) {
		out << "|p" << p.proof_id << ',' << int(p.value) << ',' << p.rule << ',' << p.source_cell_id;
		for (int parent : p.parent_proof_ids)
			out << ',' << parent;
	}
	return out.str();
}

void test_guards_and_validation()
{
	{
		Fixture f;
		auto *s = f.wire("s", 1, true);
		auto *x = f.wire("x", 1, true);
		auto *t = f.mux(s, x, s);
		auto r = f.run(f.mux_request(t, EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "mux A guard");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true);
		auto *alias = f.wire("alias");
		auto *x = f.wire("x", 1, true);
		f.module->connect(alias, s);
		auto r = f.run(f.mux_request(f.mux(alias, x, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "SigMap direct alias");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true);
		auto *x = f.wire("x", 1, true);
		auto *t = f.mux(x, s, s);
		auto r = f.run(f.mux_request(t, EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "mux B guard");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 2, true);
		auto *x = f.wire("x", 1, true);
		RTLIL::SigSpec b; b.append(x); b.append(x);
		auto *t = f.pmux(RTLIL::SigBit(s, 0), b, s);
		auto r = f.run(f.pmux_request(t, EntryKind::PmuxA));
		expect_value(r, 0, FactValue::Zero, "pmux A guard");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 2, true);
		auto *x = f.wire("x", 1, true);
		RTLIL::SigSpec b; b.append(x); b.append(RTLIL::SigBit(s, 1));
		auto *t = f.pmux(x, b, s);
		EntryRequest req = f.pmux_request(t, EntryKind::PmuxB, 1);
		auto r = f.run(req);
		expect_value(r, 0, FactValue::One, "pmux Bi guard");
		req.guard.push_back({RTLIL::SigBit(s, 0), RTLIL::State::S0});
		auto bad = f.run(req);
		require(bad.status == EntryStatus::InvalidRequest && bad.reason_code == "invalid_guard_size",
				"expanded pmux guard rejected");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true);
		auto *x = f.wire("x", 1, true);
		auto *t = f.mux(x, x, s);
		EntryRequest req = f.mux_request(t, EntryKind::MuxA);
		req.budget.max_cells = 511;
		auto r = f.run(req);
		require(r.status == EntryStatus::InvalidRequest && r.reason_code == "invalid_budget",
				"non-frozen budget rejected");
		req = f.mux_request(t, EntryKind::MuxA);
		req.physical_port_snapshot = RTLIL::State::S0;
		r = f.run(req);
		require(r.status == EntryStatus::InvalidRequest && r.reason_code == "invalid_snapshot",
				"snapshot mismatch rejected");
	}
}

void test_not_and_boolean_rules()
{
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *y = f.wire("y"); auto *x = f.wire("x", 1, true);
		f.cell(ID($not), s, y);
		auto r = f.run(f.mux_request(f.mux(y, x, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::One, "not forward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($not), x, s);
		auto r = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::Zero, "not backward");
	}
	{
		Fixture f;
		auto *g = f.wire("g", 1, true); auto *y = f.wire("y"); auto *d = f.wire("d", 1, true);
		f.cell(ID($not), RTLIL::State::Sx, y);
		auto r = f.run(f.mux_request(f.mux(y, d, g), EntryKind::MuxA));
		expect_unknown(r, 0, "not does not propagate xz");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *x = f.wire("x", 1, true); auto *y = f.wire("y");
		f.cell(ID($and), s, y, x);
		auto r = f.run(f.mux_request(f.mux(y, x, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "and decisive forward");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *y = f.wire("y"); auto *x = f.wire("x", 1, true);
		f.cell(ID($and), s, y, RTLIL::State::S1);
		auto r = f.run(f.mux_request(f.mux(x, y, s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "and all-one forward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($and), x, s, RTLIL::State::S1);
		auto r = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "and output one backward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($and), x, s, RTLIL::State::S1);
		auto r = f.run(f.mux_request(f.mux(x, d, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "and unit backward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($or), x, s, RTLIL::State::S0);
		auto one = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_value(one, 0, FactValue::One, "or output one unit");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($or), x, s, RTLIL::State::S0);
		auto zero = f.run(f.mux_request(f.mux(x, d, s), EntryKind::MuxA));
		expect_value(zero, 0, FactValue::Zero, "or output zero backward");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *x = f.wire("x", 1, true); auto *y = f.wire("y");
		f.cell(ID($or), s, y, x);
		auto r = f.run(f.mux_request(f.mux(x, y, s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "or decisive forward");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *y = f.wire("y"); auto *x = f.wire("x", 1, true);
		f.cell(ID($or), s, y, RTLIL::State::S0);
		auto r = f.run(f.mux_request(f.mux(y, x, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "or all-zero forward");
	}
	{
		Fixture f;
		auto *a = f.wire("a", 1, true); auto *b = f.wire("b", 1, true);
		auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($and), a, s, b);
		auto r = f.run(f.mux_request(f.mux(a, d, s), EntryKind::MuxA));
		expect_unknown(r, 0, "and output zero non-unique", "primary_input");
	}
	{
		Fixture f;
		auto *a = f.wire("a", 1, true); auto *b = f.wire("b", 1, true);
		auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($or), a, s, b);
		auto r = f.run(f.mux_request(f.mux(d, a, s), EntryKind::MuxB));
		expect_unknown(r, 0, "or output one non-unique", "primary_input");
	}
}

void test_mux_logic_reduce()
{
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *u = f.wire("u", 1, true); auto *y = f.wire("y");
		f.mux(RTLIL::State::S1, u, s, "inner")->setPort(ID::Y, y);
		auto r = f.run(f.mux_request(f.mux(y, u, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::One, "known mux selected side");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *u = f.wire("u", 1, true); auto *y = f.wire("y");
		f.mux(u, RTLIL::State::S1, s, "inner")->setPort(ID::Y, y);
		auto r = f.run(f.mux_request(f.mux(y, u, s), EntryKind::MuxA));
		expect_unknown(r, 0, "known mux unselected side ignored", "primary_input");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *v = f.wire("v", 2); auto *y = f.wire("y");
		RTLIL::SigSpec a; a.append(s); a.append(RTLIL::State::S0);
		f.cell(ID($logic_not), a, y);
		auto r = f.run(f.mux_request(f.mux(y, RTLIL::SigBit(v, 0), s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::One, "logic_not forward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($logic_not), x, s);
		auto r = f.run(f.mux_request(f.mux(d, RTLIL::SigBit(x, 0), s), EntryKind::MuxA));
		expect_unknown(r, 0, "logic nonzero cannot identify bit", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($logic_and), x, s, RTLIL::State::S1);
		auto r = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "logic_and unique backward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 2, true);
		f.cell(ID($logic_or), x, s, RTLIL::Const(0, 2));
		auto r = f.run(f.mux_request(f.mux(x, d, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "logic_or zero backward bit0");
		expect_value(r, 1, FactValue::Zero, "logic_or zero backward bit1");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 2, true);
		f.cell(ID($logic_and), x, s, RTLIL::Const(3, 2));
		auto r = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_unknown(r, 0, "logic_and vector nonzero bit0", "primary_input");
		expect_unknown(r, 1, "logic_and vector nonzero bit1", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 3, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($reduce_or), x, s);
		auto r = f.run(f.mux_request(f.mux(RTLIL::SigBit(x, 1), d, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "reduce_or zero backward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 3, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		RTLIL::SigSpec a; a.append(RTLIL::State::S0); a.append(RTLIL::SigBit(x, 1)); a.append(RTLIL::State::S0);
		f.cell(ID($reduce_bool), a, s);
		auto r = f.run(f.mux_request(f.mux(d, RTLIL::SigBit(x, 1), s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "reduce_bool unique unit");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($reduce_or), x, s);
		auto r = f.run(f.mux_request(f.mux(d, RTLIL::SigBit(x, 0), s), EntryKind::MuxB));
		expect_unknown(r, 0, "reduce nonzero cannot identify bit", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		f.cell(ID($reduce_and), x, s);
		auto r = f.run(f.mux_request(f.mux(d, RTLIL::SigBit(x, 0), s), EntryKind::MuxB));
		expect_value(r, 0, FactValue::One, "reduce_and one backward");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true); auto *s = f.wire("s"); auto *d = f.wire("d", 1, true);
		RTLIL::SigSpec a; a.append(RTLIL::State::S1); a.append(x); a.append(RTLIL::State::S1);
		f.cell(ID($reduce_and), a, s);
		auto r = f.run(f.mux_request(f.mux(x, d, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "reduce_and unique unit");
	}
}

void test_comparisons()
{
	for (bool ne : {false, true}) {
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 2, true);
		RTLIL::Const c(2, 2);
		f.cell(ne ? ID($ne) : ID($eq), x, s, c);
		EntryKind kind = ne ? EntryKind::MuxA : EntryKind::MuxB;
		RTLIL::Cell *target = ne ? f.mux(x, d, s) : f.mux(d, x, s);
		auto r = f.run(f.mux_request(target, kind));
		expect_value(r, 0, FactValue::Zero, ne ? "ne false reverse bit0" : "eq true reverse bit0");
		expect_value(r, 1, FactValue::One, ne ? "ne false reverse bit1" : "eq true reverse bit1");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 2, true);
		f.cell(ID($eq), x, s, RTLIL::Const(2, 2));
		auto r = f.run(f.mux_request(f.mux(x, d, s), EntryKind::MuxA));
		expect_unknown(r, 0, "eq false no per-bit reverse", "primary_input");
		expect_unknown(r, 1, "eq false no per-bit reverse", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *d = f.wire("d", 2, true);
		f.cell(ID($eq), x, s, RTLIL::Const(std::vector<RTLIL::State>{RTLIL::State::Sx, RTLIL::State::S1}));
		auto r = f.run(f.mux_request(f.mux(d, x, s), EntryKind::MuxB));
		expect_unknown(r, 0, "eq x constant rejected", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 2, true); auto *s = f.wire("s"); auto *g = f.wire("g", 1, true);
		auto *cmp = f.cell(ID($eq), x, s, RTLIL::State::S0);
		cmp->setParam(ID::A_SIGNED, 1);
		cmp->setParam(ID::B_SIGNED, 0);
		auto r = f.run(f.mux_request(f.mux(s, RTLIL::State::S0, g), EntryKind::MuxA));
		expect_unknown(r, 0, "comparison signed mismatch", "comparison_semantics_ambiguous");
	}
	{
		Fixture f;
		auto *g = f.wire("g", 1, true); auto *y = f.wire("y"); auto *d = f.wire("d", 1, true);
		f.cell(ID($eq), RTLIL::Const(3, 2), y, RTLIL::Const(3, 2));
		auto r = f.run(f.mux_request(f.mux(y, d, g), EntryKind::MuxA));
		expect_value(r, 0, FactValue::One, "ConstEval equality forward");
		require(r.proof_records[bit(r, 0).proof_id].rule == "consteval_forward",
				"ConstEval forward proof is recorded");
	}
}

void test_boundaries_conflict_and_constants()
{
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *u = f.wire("u", 1, true); auto *good = f.wire("good"); auto *bad = f.wire("bad");
		f.cell(ID($not), s, good);
		f.cell(ID($xor), u, bad, RTLIL::State::S1);
		RTLIL::SigSpec data; data.append(good); data.append(bad);
		auto r = f.run(f.mux_request(f.mux(data, RTLIL::Const(0, 2), s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::One, "independent good bit");
		expect_unknown(r, 1, "unsupported isolated bit", "unsupported_cell");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *q = f.wire("q"); auto *d = f.wire("d", 1, true);
		auto *ff = f.module->addCell(eid("ff"), ID($dff));
		ff->setParam(ID::WIDTH, 1); ff->setPort(ID::D, d); ff->setPort(ID::CLK, s); ff->setPort(ID::Q, q);
		auto r = f.run(f.mux_request(f.mux(q, d, s), EntryKind::MuxA));
		expect_unknown(r, 0, "sequential boundary", "sequential_boundary");
	}
	{
		Fixture f;
		auto *bb = f.design.addModule(eid("bb")); bb->set_bool_attribute(ID::blackbox);
		auto *bb_y = bb->addWire(ID::Y); bb_y->port_output = true; bb_y->port_id = 1; bb->fixup_ports();
		auto *s = f.wire("s", 1, true); auto *q = f.wire("q"); auto *d = f.wire("d", 1, true);
		auto *c = f.module->addCell(eid("bbinst"), bb->name); c->setPort(ID::Y, q);
		auto r = f.run(f.mux_request(f.mux(q, d, s), EntryKind::MuxA));
		expect_unknown(r, 0, "blackbox boundary", "blackbox_boundary");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *q = f.wire("q"); auto *d = f.wire("d", 1, true);
		auto *mem = f.module->addCell(eid("mem"), ID($memrd)); mem->setPort(ID::DATA, q);
		auto r = f.run(f.mux_request(f.mux(q, d, s), EntryKind::MuxA));
		expect_unknown(r, 0, "memory boundary", "memory_boundary");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *q = f.wire("q"); auto *a = f.wire("a", 1, true);
		f.cell(ID($not), a, q); f.cell(ID($not), s, q);
		auto r = f.run(f.mux_request(f.mux(q, a, s), EntryKind::MuxA));
		expect_unknown(r, 0, "multiple driver", "multiple_driver");
	}
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *x = f.wire("x", 1, true);
		RTLIL::SigSpec data; data.append(RTLIL::State::S1); data.append(RTLIL::State::Sx); data.append(x);
		auto r = f.run(f.mux_request(f.mux(data, RTLIL::Const(0, 3), s), EntryKind::MuxA));
		require(GetSize(r.bits) == 2, "original binary constant omitted");
		expect_unknown(r, 1, "original x preserved", "original_xz");
		expect_unknown(r, 2, "primary input isolated", "primary_input");
	}
	{
		Fixture f;
		auto *x = f.wire("x", 1, true);
		auto *t = f.mux(x, x, RTLIL::State::S1);
		auto r = f.run(f.mux_request(t, EntryKind::MuxA));
		require(r.status == EntryStatus::FactConflict && r.reason_code == "fact_conflict", "fact conflict status");
		expect_unknown(r, 0, "conflict invalidates entry", "fact_conflict");
	}
}

void test_proofs_determinism_and_budgets()
{
	{
		Fixture f;
		auto *s = f.wire("s", 1, true); auto *y = f.wire("y"); auto *x = f.wire("x", 1, true);
		f.cell(ID($not), s, y);
		EntryRequest req = f.mux_request(f.mux(y, x, s), EntryKind::MuxA);
		auto first = f.run(req), second = f.run(req);
		require(digest(first) == digest(second), "repeat analysis deterministic");
		const BitConclusion &b = bit(first, 0);
		std::vector<bool> visiting(first.proof_records.size(), false);
		trace_proof(first, b.proof_id, visiting);
	}
	{
		Fixture f;
		auto *s = f.wire("s"); auto *x = f.wire("x", 1, true);
		f.cell(ID($not), RTLIL::State::S1, s);
		auto r = f.run(f.mux_request(f.mux(s, x, s), EntryKind::MuxA));
		expect_value(r, 0, FactValue::Zero, "shortest proof choice");
		require(r.proof_records[bit(r, 0).proof_id].rule == "guard",
				"shorter guard proof wins over derived proof");
	}

	for (BudgetKind kind : {BudgetKind::Cells, BudgetKind::Bits, BudgetKind::Events}) {
		BudgetTracker tracker(3, 3, 3);
		auto take = [&]() {
			if (kind == BudgetKind::Cells) return tracker.take_cell();
			if (kind == BudgetKind::Bits) return tracker.take_bit();
			return tracker.take_event();
		};
		require(take() && take(), "budget below limit accepted");
		require(take(), "budget exact limit accepted");
		require(tracker.exhausted() == BudgetKind::None, "exact limit not exhausted");
		require(!take() && tracker.exhausted() == kind, "limit plus one exhausted");
	}

	for (int count : {512, 513}) {
		Fixture f;
		auto *s = f.wire("s", 1, true); RTLIL::SigBit current = s;
		for (int i = 0; i < count; ++i) {
			auto *next = f.wire("chain");
			f.cell(ID($not), current, next);
			current = next;
		}
		auto *x = f.wire("x", 1, true);
		auto r = f.run(f.mux_request(f.mux(current, x, s), EntryKind::MuxA));
		if (count == 512) {
			require(r.status == EntryStatus::Normal && r.usage.visited_cells == 512,
					"512-cell limit succeeds exactly");
			expect_value(r, 0, FactValue::Zero, "512-cell proof");
		} else {
			require(r.status == EntryStatus::BudgetExhausted && r.reason_code == "budget_cells_exhausted" &&
					r.usage.visited_cells == 512, "513th cell exhausts cell budget");
		}
	}

	for (int inputs : {8190, 8191, 8192}) {
		Fixture f;
		auto *s = f.wire("s", 1, true);
		RTLIL::SigSpec a(s);
		for (int i = 1; i < inputs; ++i)
			a.append(f.wire("wide", 1, true));
		auto *y = f.wire("wide_y");
		f.cell(ID($reduce_or), a, y);
		auto *x = f.wire("x", 1, true);
		auto r = f.run(f.mux_request(f.mux(y, x, s), EntryKind::MuxA));
		if (inputs <= 8191)
			require(r.status == EntryStatus::Normal && r.usage.visited_bits == std::size_t(inputs + 1),
					"bit budget at or below limit succeeds");
		else
			require(r.status == EntryStatus::BudgetExhausted && r.reason_code == "budget_bits_exhausted" &&
					r.usage.visited_bits == 8192, "8193rd bit exhausts bit budget");
	}

	if (std::getenv("PMUX_ANALYZER_SLOW_TESTS") != nullptr) {
		Fixture f;
		auto *s = f.wire("event_guard", 512, true);
		RTLIL::SigSpec data;
		for (int i = 0; i < 512; ++i) {
			auto *y = f.wire("event_y");
			f.cell(ID($logic_not), s, y);
			data.append(y);
		}
		RTLIL::SigSpec branches(RTLIL::State::S0, 512 * 512);
		auto r = f.run(f.pmux_request(f.pmux(data, branches, s), EntryKind::PmuxA));
		require(r.status == EntryStatus::BudgetExhausted && r.reason_code == "budget_events_exhausted" &&
				r.usage.propagation_events == 262144, "event limit plus one exhausts analyzer budget");
	}
}

void run_all_tests()
{
	test_guards_and_validation();
	test_not_and_boolean_rules();
	test_mux_logic_reduce();
	test_comparisons();
	test_boundaries_conflict_and_constants();
	test_proofs_determinism_and_budgets();
	log("PMUX analyzer selftest: all checks passed (%d assertions)\n", checks);
}

struct SelftestRunner {
	SelftestRunner() { run_all_tests(); }
} selftest_runner;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
