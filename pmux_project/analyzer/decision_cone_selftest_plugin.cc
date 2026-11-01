#include "pmux_v02_decision_cone_proof_api.h"

#include "backends/rtlil/rtlil_backend.h"
#include "kernel/log.h"

#include <algorithm>
#include <sstream>

YOSYS_NAMESPACE_BEGIN

namespace PmuxOpt {
namespace {

int decision_checks = 0;

void require(bool condition, const char *message)
{
	++decision_checks; if (!condition) log_error("PMUX decision-cone selftest failed: %s\n", message);
}

RTLIL::IdString id(const std::string &name) { return RTLIL::IdString("\\" + name); }

struct Fixture {
	RTLIL::Design design;
	RTLIL::Module *module;
	int serial = 0;
	RTLIL::Wire *source;
	RTLIL::Wire *external;
	RTLIL::Wire *noise;
	RTLIL::Cell *cmp0;
	RTLIL::Cell *cmp1;
	RTLIL::Cell *data;
	RTLIL::Cell *sink;

	explicit Fixture(const std::string &tag = "renamed_structure") : module(design.addModule(id(tag)))
	{
		source = wire("state_alias", 4, true); external = wire("gate_alias", 1, true); noise = wire("free_alias", 1, true);
		cmp0 = eq(source, RTLIL::Const(0x9, 4)); cmp1 = eq(source, RTLIL::Const(0xf, 4));
		data = mux(RTLIL::SigSpec(source).extract(2, 2), RTLIL::SigSpec(source).extract(0, 2), cmp0->getPort(ID::Y)[0]);
		RTLIL::Cell *any = reduce(ID($reduce_bool), RTLIL::SigSpec({cmp0->getPort(ID::Y)[0], cmp1->getPort(ID::Y)[0]}));
		RTLIL::Cell *none = not_cell(any->getPort(ID::Y)[0]);
		RTLIL::Cell *guard = reduce(ID($reduce_and), RTLIL::SigSpec({RTLIL::SigBit(source, 3), RTLIL::SigBit(external, 0), none->getPort(ID::Y)[0]}));
		sink = dffe(data->getPort(ID::Y), guard->getPort(ID::Y)[0]);
	}

	RTLIL::Wire *wire(const std::string &base, int width = 1, bool input = false)
	{
		RTLIL::Wire *result = module->addWire(id(base + std::to_string(serial++)), width); result->port_input = input; return result;
	}
	RTLIL::Cell *mux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b, RTLIL::SigBit select)
	{
		RTLIL::Cell *cell = module->addCell(id("choice_alias" + std::to_string(serial++)), ID($mux));
		RTLIL::Wire *y = wire("choice_result", GetSize(a)); cell->setParam(ID::WIDTH, GetSize(a));
		cell->setPort(ID::A, a); cell->setPort(ID::B, b); cell->setPort(ID::S, select); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *eq(const RTLIL::SigSpec &a, const RTLIL::Const &b)
	{
		RTLIL::Cell *cell = module->addCell(id("predicate_alias" + std::to_string(serial++)), ID($eq)); RTLIL::Wire *y = wire("predicate_result");
		cell->setParam(ID::A_WIDTH, GetSize(a)); cell->setParam(ID::B_WIDTH, GetSize(b)); cell->setParam(ID::A_SIGNED, 0);
		cell->setParam(ID::B_SIGNED, 0); cell->setParam(ID::Y_WIDTH, 1); cell->setPort(ID::A, a); cell->setPort(ID::B, b); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *reduce(RTLIL::IdString type, const RTLIL::SigSpec &a)
	{
		RTLIL::Cell *cell = module->addCell(id("reduce_alias" + std::to_string(serial++)), type); RTLIL::Wire *y = wire("reduce_result");
		cell->setParam(ID::A_WIDTH, GetSize(a)); cell->setParam(ID::A_SIGNED, 0); cell->setParam(ID::Y_WIDTH, 1);
		cell->setPort(ID::A, a); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *not_cell(RTLIL::SigBit a)
	{
		RTLIL::Cell *cell = module->addCell(id("invert_alias" + std::to_string(serial++)), ID($not)); RTLIL::Wire *y = wire("invert_result");
		cell->setParam(ID::A_WIDTH, 1); cell->setParam(ID::A_SIGNED, 0); cell->setParam(ID::Y_WIDTH, 1);
		cell->setPort(ID::A, a); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *dffe(const RTLIL::SigSpec &d, RTLIL::SigBit en)
	{
		RTLIL::Cell *cell = module->addCell(id("state_boundary" + std::to_string(serial++)), ID($dffe));
		RTLIL::Wire *q = wire("state_output", GetSize(d)); RTLIL::Wire *clk = wire("clock_alias", 1, true);
		cell->setParam(ID::WIDTH, GetSize(d)); cell->setParam(ID::CLK_POLARITY, 1); cell->setParam(ID::EN_POLARITY, 1);
		cell->setPort(ID::D, d); cell->setPort(ID::Q, q); cell->setPort(ID::CLK, clk); cell->setPort(ID::EN, en); return cell;
	}
	std::string fingerprint()
	{
		module->fixup_ports(); std::ostringstream out; RTLIL_BACKEND::dump_module(out, "", module, &design, false); return out.str();
	}
	DecisionConeProof run(DecisionConeProofRequest request = {})
	{
		request.module = module; request.module_id = module->name.str(); std::string before = fingerprint();
		DecisionConeProof result = make_decision_cone_analyzer()->analyze(request);
		require(before == fingerprint(), "decision-cone analyzer changed RTLIL fingerprint"); return result;
	}
};

bool authorized(const DecisionConeProof &proof)
{
	return proof.status == V02ProofStatus::Normal && proof.proven_all_obligations && proof.structure_complete &&
		proof.current_cycle_guard_proved && proof.selection_semantics_proved && proof.guard_inside_equivalence_proved &&
		proof.guard_outside_equivalence_proved && proof.all_sink_bits_proved && proof.all_fanout_covered &&
		proof.all_sequential_boundaries_proved && proof.normalized_dag_complete;
}

int consume_node(const DecisionConeProof &proof, int root, int address)
{
	int node = root;
	while (node >= 0 && node < GetSize(proof.normalized_dag) && proof.normalized_dag[node].kind == DecisionDagNodeKind::Test) {
		const auto &item = proof.normalized_dag[node];
		require(item.low_node < item.node_id && item.high_node < item.node_id, "consumer sees topological DAG children");
		node = (address >> item.input_index) & 1 ? item.high_node : item.low_node;
	}
	return node == 1 ? 1 : node == 0 ? 0 : -1;
}

void consume_and_check(const DecisionConeProof &proof)
{
	for (const DecisionSinkProof &sink : proof.affected_sinks) {
		require(sink.normalized_root_node >= 0 && sink.normalized_root_node < GetSize(proof.normalized_dag), "consumer receives valid sink root");
		for (int row = 0; row < GetSize(sink.care_mask_lsb_address); ++row) if (sink.care_mask_lsb_address[row] == RTLIL::State::S1)
			require(consume_node(proof, sink.normalized_root_node, row) == (sink.value_mask_lsb_address[row] == RTLIL::State::S1),
				"test-only consumer reconstructs every care row");
	}
}

DecisionConeProofRequest anchored(const DecisionConeProof &proof)
{
	DecisionConeProofRequest request;
	for (const auto &support : proof.support_lsb_first) request.expected_support_lsb_first.push_back(support.bit);
	for (const auto &sink : proof.affected_sinks) request.expected_sequential_sinks.push_back(sink.sequential_snapshot);
	request.expected_source_cells = proof.source_cells; return request;
}

void run_tests()
{
	{
		Fixture f("all_identifiers_changed"); auto proof = f.run(); require(authorized(proof), "renamed isomorphic structure proves");
		require(proof.support_lsb_first.size() == 3 && proof.affected_sinks.size() == 2, "complete support and sink bits returned");
		consume_and_check(proof);
	}
	{
		Fixture f; f.cmp0->setPort(ID::B, RTLIL::Const(0xb, 4)); f.cmp1->setPort(ID::B, RTLIL::Const(0xd, 4));
		require(authorized(f.run()), "changed binary comparator constants are structure-driven");
	}
	{
		Fixture f; RTLIL::SigSpec a = f.data->getPort(ID::A), b = f.data->getPort(ID::B); f.data->setPort(ID::A, b); f.data->setPort(ID::B, a);
		auto proof = f.run(); require(authorized(proof), "changed branch data and mux priority are reproved");
		require(std::any_of(proof.decision_relations.begin(), proof.decision_relations.end(), [](const auto &r) {
			return r.kind == DecisionRelationKind::MuxPriority && r.last_assignment_semantics_proved; }), "mux assignment priority recorded");
	}
	{
		Fixture f; f.sink->setPort(ID::EN, f.external); require(!authorized(f.run()), "missing dominating source guard rejected");
	}
	{
		Fixture f; RTLIL::Cell *second = f.dffe(f.data->getPort(ID::Y), RTLIL::SigBit(f.noise, 0)); (void)second;
		require(!authorized(f.run()), "guard covering only part of affected sinks rejected");
	}
	{
		Fixture f; RTLIL::SigSpec branch = f.data->getPort(ID::B); f.data->type = ID($pmux); f.data->setParam(ID::S_WIDTH, 2);
		f.data->setPort(ID::B, RTLIL::SigSpec({branch, branch})); f.data->setPort(ID::S, RTLIL::SigSpec({f.cmp0->getPort(ID::Y)[0], f.cmp0->getPort(ID::Y)[0]}));
		auto proof = f.run(); require(!authorized(proof) && proof.reason_code == DecisionConeReasonCode::SelectOverlapUnproved,
			"overlapping native pmux selects rejected");
	}
	{
		Fixture f; RTLIL::Wire *visible = f.module->addWire(id("observable_predicate"), 1); visible->port_output = true;
		f.module->connect(visible, f.cmp0->getPort(ID::Y)); require(!authorized(f.run()), "uncovered observable comparator fanout rejected");
	}
	{
		Fixture f; auto good = f.run(); require(authorized(good), "snapshot source proof exists"); auto request = anchored(good);
		for (auto &param : request.expected_sequential_sinks[0].cell_snapshot.parameters) if (param.parameter == ID::EN_POLARITY) param.value = RTLIL::Const(0, 1);
		auto bad = f.run(request); require(!authorized(bad) && bad.reason_code == DecisionConeReasonCode::SnapshotMismatch, "enable snapshot mismatch rejected");
	}
	{
		Fixture f; auto good = f.run(); auto request = anchored(good);
		request.expected_sequential_sinks[0].cell_snapshot.parameters.push_back({ID::SRST_POLARITY, RTLIL::Const(1, 1)});
		require(!authorized(f.run(request)), "reset snapshot mismatch rejected");
	}
	{
		Fixture f; auto good = f.run(); auto request = anchored(good);
		for (auto &port : request.expected_sequential_sinks[0].cell_snapshot.ports) if (port.port == ID::D) port.signal = request.expected_sequential_sinks[0].cell_snapshot.ports[0].signal;
		require(!authorized(f.run(request)), "D/hold snapshot mismatch rejected");
	}
	{
		Fixture f; auto good = f.run(); auto request = anchored(good);
		request.expected_sequential_sinks[0].q_wire_attributes.push_back({ID::init, RTLIL::Const(1, 1)});
		require(!authorized(f.run(request)), "initialization snapshot mismatch rejected");
	}
	{
		Fixture f; RTLIL::SigSpec d = f.sink->getPort(ID::D); d[1] = RTLIL::State::Sx; f.sink->setPort(ID::D, d);
		require(!authorized(f.run()), "partial sink proof cannot authorize whole cone");
	}
	{
		Fixture f; std::vector<RTLIL::State> bad = {RTLIL::State::S1, RTLIL::State::S0, RTLIL::State::S0, RTLIL::State::Sx};
		f.cmp0->setPort(ID::B, RTLIL::Const(bad)); require(!authorized(f.run()), "x/z comparator rejected");
	}
	{
		Fixture f; f.cmp0->setParam(ID::A_SIGNED, 1); require(!authorized(f.run()), "ambiguous signed comparison group rejected");
	}
	{
		Fixture f; f.cmp0->setParam(ID::B_WIDTH, 3); require(!authorized(f.run()), "inconsistent comparison width rejected");
	}
	{
		Fixture f; RTLIL::Cell *driver = f.module->addCell(id("second_driver"), ID($not));
		driver->setParam(ID::A_WIDTH, 1); driver->setParam(ID::A_SIGNED, 0); driver->setParam(ID::Y_WIDTH, 1);
		driver->setPort(ID::A, RTLIL::State::S0); driver->setPort(ID::Y, f.data->getPort(ID::Y)[0]); require(!authorized(f.run()), "multiple driver rejected");
	}
	{
		Fixture f; RTLIL::Cell *unsupported = f.module->addCell(id("unsupported_sum"), ID($add)); RTLIL::Wire *y = f.wire("sum_result", 2);
		unsupported->setParam(ID::A_WIDTH, 2); unsupported->setParam(ID::B_WIDTH, 2); unsupported->setParam(ID::Y_WIDTH, 2);
		unsupported->setParam(ID::A_SIGNED, 0); unsupported->setParam(ID::B_SIGNED, 0);
		unsupported->setPort(ID::A, f.data->getPort(ID::Y)); unsupported->setPort(ID::B, RTLIL::Const(0, 2)); unsupported->setPort(ID::Y, y);
		f.sink->setPort(ID::D, y); require(!authorized(f.run()), "unsupported combinational cell rejected");
	}
	{
		Fixture f; RTLIL::SigSpec original = f.sink->getPort(ID::D); RTLIL::SigSpec inverted;
		for (RTLIL::SigBit bit : original) inverted.append(f.not_cell(bit)->getPort(ID::Y));
		RTLIL::Cell *choice = f.mux(original, inverted, RTLIL::SigBit(f.noise, 0)); f.sink->setPort(ID::D, choice->getPort(ID::Y));
		auto proof = f.run(); require(!authorized(proof) && proof.reason_code == DecisionConeReasonCode::FactConflict,
			"free non-support data creates fact conflict and is rejected");
	}
	{
		Fixture f; auto good = f.run(); auto request = anchored(good); request.expected_source_cells[0].type = ID($not);
		auto bad = f.run(request); require(!authorized(bad) && bad.reason_code == DecisionConeReasonCode::SnapshotMismatch, "source snapshot mismatch rejected");
	}
	{
		Fixture f; auto good = f.run(); auto request = anchored(good); request.expected_sequential_sinks[0].q_wire_port_output = true;
		require(!authorized(f.run(request)), "output-port boundary snapshot mismatch rejected");
	}
	for (int kind = 0; kind < 3; ++kind) {
		Fixture f; DecisionConeProofRequest request;
		if (kind == 0) request.budget.max_cells = 1;
		if (kind == 1) request.budget.max_bits = 1;
		if (kind == 2) request.budget.max_events = 1;
		auto proof = f.run(request);
		if (authorized(proof) || proof.status != V02ProofStatus::BudgetExhausted)
			log("decision budget diagnostic kind=%d status=%d reason=%s\n", kind, int(proof.status), decision_cone_reason_code_name(proof.reason_code));
		require(!authorized(proof) && proof.status == V02ProofStatus::BudgetExhausted, "cell/bit/event budget exhaustion rejects");
	}
	log("PMUX decision-cone selftest: all checks passed (%d assertions)\n", decision_checks);
}

struct Runner { Runner() { run_tests(); } } runner;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
