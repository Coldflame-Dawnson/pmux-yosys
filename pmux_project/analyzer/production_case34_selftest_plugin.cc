#include "pmux_v02_production_proof_api.h"
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

int production_checks = 0;

void require(bool condition, const std::string &message)
{
	++production_checks;
	if (!condition) log_error("PMUX production case34 selftest failed: %s\n", message.c_str());
}

RTLIL::IdString id(const std::string &name) { return RTLIL::escape_id(name); }

CellSnapshot snapshot(const RTLIL::Cell *cell)
{
	CellSnapshot out; out.cell_id = cell->name.str(); out.type = cell->type;
	for (const auto &conn : cell->connections()) out.ports.push_back({conn.first, conn.second});
	for (const auto &param : cell->parameters) out.parameters.push_back({param.first, param.second});
	return out;
}

struct Fixture {
	RTLIL::Design design;
	RTLIL::Module *module;
	int serial = 0;

	Fixture() : module(design.addModule(id("production_fixture"))) { }
	RTLIL::Wire *wire(const std::string &base, int width = 1, bool input = false)
	{
		RTLIL::Wire *value = module->addWire(id(base + std::to_string(serial++)), width);
		value->port_input = input; return value;
	}
	RTLIL::Cell *mux(const RTLIL::SigSpec &a, const RTLIL::SigSpec &b, RTLIL::SigBit select)
	{
		RTLIL::Cell *cell = module->addCell(id("mux" + std::to_string(serial++)), ID($mux));
		RTLIL::Wire *y = wire("my", GetSize(a));
		cell->setParam(ID::WIDTH, GetSize(a)); cell->setPort(ID::A, a); cell->setPort(ID::B, b);
		cell->setPort(ID::S, select); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *eq(const RTLIL::SigSpec &a, const RTLIL::Const &b)
	{
		RTLIL::Cell *cell = module->addCell(id("eq" + std::to_string(serial++)), ID($eq));
		RTLIL::Wire *y = wire("ey");
		cell->setParam(ID::A_WIDTH, GetSize(a)); cell->setParam(ID::B_WIDTH, GetSize(b));
		cell->setParam(ID::A_SIGNED, 0); cell->setParam(ID::B_SIGNED, 0); cell->setParam(ID::Y_WIDTH, 1);
		cell->setPort(ID::A, a); cell->setPort(ID::B, b); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *reduce(RTLIL::IdString type, const RTLIL::SigSpec &a)
	{
		RTLIL::Cell *cell = module->addCell(id("logic" + std::to_string(serial++)), type);
		RTLIL::Wire *y = wire("ly"); cell->setParam(ID::A_WIDTH, GetSize(a)); cell->setParam(ID::A_SIGNED, 0);
		cell->setParam(ID::Y_WIDTH, 1); cell->setPort(ID::A, a); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *not_cell(RTLIL::SigBit a)
	{
		RTLIL::Cell *cell = module->addCell(id("not" + std::to_string(serial++)), ID($not));
		RTLIL::Wire *y = wire("ny"); cell->setParam(ID::A_WIDTH, 1); cell->setParam(ID::A_SIGNED, 0);
		cell->setParam(ID::Y_WIDTH, 1); cell->setPort(ID::A, a); cell->setPort(ID::Y, y); return cell;
	}
	RTLIL::Cell *dffe(const RTLIL::SigSpec &d, RTLIL::SigBit en)
	{
		RTLIL::Cell *cell = module->addCell(id("ff" + std::to_string(serial++)), ID($dffe));
		RTLIL::Wire *q = wire("q", GetSize(d)); RTLIL::Wire *clk = wire("clk", 1, true);
		cell->setParam(ID::WIDTH, GetSize(d)); cell->setParam(ID::CLK_POLARITY, 1); cell->setParam(ID::EN_POLARITY, 1);
		cell->setPort(ID::D, d); cell->setPort(ID::Q, q); cell->setPort(ID::CLK, clk); cell->setPort(ID::EN, en); return cell;
	}
	RTLIL::Cell *adffe(const RTLIL::SigSpec &d, RTLIL::SigBit en, RTLIL::SigBit arst)
	{
		RTLIL::Cell *cell = module->addCell(id("aff" + std::to_string(serial++)), ID($adffe));
		RTLIL::Wire *q = wire("aq", GetSize(d)); RTLIL::Wire *clk = wire("aclk", 1, true);
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
	ProductionCase34ProofBundle run(ProductionCase34ProofRequest request = {})
	{
		request.module = module; request.module_id = module->name.str();
		std::string before = fingerprint(); auto result = make_production_case34_analyzer()->analyze(request);
		require(before == fingerprint(), "production analyzer changed RTLIL fingerprint"); return result;
	}
};

struct GuardFixture : Fixture {
	RTLIL::Wire *source;
	RTLIL::Wire *external;
	RTLIL::Cell *cmp0;
	RTLIL::Cell *cmp1;
	RTLIL::Cell *sink;

	GuardFixture()
	{
		source = wire("source", 4, true); external = wire("external", 1, true);
		cmp0 = eq(source, RTLIL::Const(0x9, 4)); cmp1 = eq(source, RTLIL::Const(0xf, 4));
		RTLIL::Cell *data = mux(RTLIL::SigSpec(source).extract(2, 2), RTLIL::SigSpec(source).extract(0, 2), cmp0->getPort(ID::Y)[0]);
		RTLIL::Cell *any = reduce(ID($reduce_bool), RTLIL::SigSpec({cmp0->getPort(ID::Y)[0], cmp1->getPort(ID::Y)[0]}));
		RTLIL::Cell *none = not_cell(any->getPort(ID::Y)[0]);
		RTLIL::Cell *guard = reduce(ID($reduce_and), RTLIL::SigSpec({RTLIL::SigBit(source, 3), RTLIL::SigBit(external, 0), none->getPort(ID::Y)[0]}));
		sink = dffe(data->getPort(ID::Y), guard->getPort(ID::Y)[0]);
	}
};

struct TableFixture : Fixture {
	RTLIL::Wire *address;
	RTLIL::Wire *enable;
	RTLIL::Wire *reset;
	RTLIL::SigSpec root;
	RTLIL::Cell *sink0;
	RTLIL::Cell *sink1;

	RTLIL::SigSpec scalar_tree(int output_bit, int level, int base)
	{
		if (level == 0) return RTLIL::Const((((base * 0x25u) + 0x270u) >> output_bit) & 1u, 1);
		RTLIL::SigSpec a = scalar_tree(output_bit, level - 1, base);
		RTLIL::SigSpec b = scalar_tree(output_bit, level - 1, base + (1 << (level - 1)));
		return mux(a, b, RTLIL::SigBit(address, level - 1))->getPort(ID::Y);
	}

	TableFixture()
	{
		address = wire("address", 5, true); enable = wire("enable", 1, true); reset = wire("reset", 1, true);
		RTLIL::SigSpec low, high;
		for (int bit = 0; bit < 10; ++bit) {
			low.append(scalar_tree(bit, 4, 0)); high.append(scalar_tree(bit, 4, 16));
		}
		root = mux(low, high, RTLIL::SigBit(address, 4))->getPort(ID::Y);
		sink0 = adffe(root.extract(0, 6), RTLIL::SigBit(enable, 0), RTLIL::SigBit(reset, 0));
		sink1 = adffe(root.extract(6, 4), RTLIL::SigBit(enable, 0), RTLIL::SigBit(reset, 0));
	}
	ProductionCase34ProofRequest anchored(const ProductionConstantTableProof &proof)
	{
		ProductionCase34ProofRequest request;
		request.expected_address_bits_lsb_first = proof.address_bits_lsb_first;
		request.expected_output_bits_lsb_first = proof.output_bits_lsb_first;
		request.expected_sequential_sinks = proof.sequential_sinks;
		return request;
	}
};

const ProductionGuardedConeProof *guard_proof(const ProductionCase34ProofBundle &bundle)
{
	for (const auto &proof : bundle.guarded_cones) if (proof.status == V02ProofStatus::Normal) return &proof;
	return nullptr;
}

const ProductionConstantTableProof *table_proof(const ProductionCase34ProofBundle &bundle)
{
	for (const auto &proof : bundle.constant_tables) if (proof.status == V02ProofStatus::Normal) return &proof;
	return nullptr;
}

void test_guarded()
{
	{
		GuardFixture f; auto bundle = f.run(); const auto *proof = guard_proof(bundle);
		require(proof && proof->proven_all_obligations && proof->source_width == 4 && proof->removed_bit_indices == std::vector<int>{3},
			"production reduce/not/enable guard proves comparator narrowing");
		require(proof && !proof->folded_data_bits.empty() && proof->all_sequential_boundaries_proved,
			"production guard records folds and sequential boundary");
	}
	{
		GuardFixture f; f.sink->setPort(ID::EN, f.external);
		require(!guard_proof(f.run()), "guard entering unrelated combinational logic does not dominate target");
	}
	{
		GuardFixture f; RTLIL::Cell *inverted = f.not_cell(RTLIL::SigBit(f.source, 3)); f.sink->setPort(ID::EN, inverted->getPort(ID::Y));
		require(!guard_proof(f.run()), "wrong guard polarity rejected");
	}
	{
		GuardFixture f; RTLIL::Wire *observed = f.module->addWire(id("observed"), 1); observed->port_output = true;
		f.module->connect(observed, f.cmp0->getPort(ID::Y));
		require(!guard_proof(f.run()), "extra observable comparator fanout rejected");
	}
	{
		GuardFixture f; f.sink->setPort(ID::EN, f.external);
		require(!guard_proof(f.run()), "partial comparator-bit fact cannot authorize whole cone");
	}
	{
		GuardFixture f; std::vector<RTLIL::State> bad = {RTLIL::State::S1, RTLIL::State::S0, RTLIL::State::S0, RTLIL::State::Sx};
		f.cmp0->setPort(ID::B, RTLIL::Const(bad));
		require(!guard_proof(f.run()), "x/z comparator constant rejected");
	}
	{
		GuardFixture f; f.module->connect(f.sink->getPort(ID::EN), RTLIL::State::S0);
		require(!guard_proof(f.run()), "conflicting active guard fact rejected");
	}
}

void test_table()
{
	{
		TableFixture f; auto bundle = f.run(); const auto *proof = table_proof(bundle);
		require(proof && proof->address_width == 5 && proof->output_width == 10 && proof->truth_table.size() == 32,
			"bit-sliced 5-to-10 production table proves");
		require(proof && proof->truth_table[0].as_string() == "1001110000" && proof->sequential_sinks.size() == 10,
			"table encoding and all logical-to-physical sink bits recorded");
		require(proof && std::count_if(proof->sequential_sinks.begin(), proof->sequential_sinks.end(),
			[&](const auto &sink) { return sink.cell == f.sink0; }) == 6, "one physical sink covers multiple logical bits");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "anchor source proof exists");
		auto request = f.anchored(*proof); std::reverse(request.expected_address_bits_lsb_first.begin(), request.expected_address_bits_lsb_first.end());
		auto bad = f.run(request); require(!table_proof(bad) && !bad.constant_tables.empty() &&
			bad.constant_tables[0].reason_code == V02ReasonCode::AddressOrderUnknown, "address order conflict rejected");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "output anchor source proof exists");
		auto request = f.anchored(*proof); std::reverse(request.expected_output_bits_lsb_first.begin(), request.expected_output_bits_lsb_first.end());
		auto bad = f.run(request); require(!table_proof(bad) && !bad.constant_tables.empty() &&
			bad.constant_tables[0].reason_code == V02ReasonCode::OutputOrderUnknown, "output order conflict rejected");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "sink anchor source proof exists");
		auto request = f.anchored(*proof); request.expected_sequential_sinks[0].snapshot.parameters[0].value = RTLIL::Const(1, 1);
		auto bad = f.run(request); require(!table_proof(bad) && !bad.constant_tables.empty() &&
			bad.constant_tables[0].reason_code == V02ReasonCode::SequentialBoundaryChanged, "reset snapshot change rejected");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "enable anchor source proof exists");
		auto request = f.anchored(*proof);
		for (auto &param : request.expected_sequential_sinks[0].snapshot.parameters)
			if (param.parameter == ID::EN_POLARITY) param.value = RTLIL::Const(0, 1);
		auto bad = f.run(request); require(!table_proof(bad), "enable/hold snapshot change rejected");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "hold anchor source proof exists");
		auto request = f.anchored(*proof);
		for (auto &port : request.expected_sequential_sinks[0].snapshot.ports)
			if (port.port == ID::D) port.signal = request.expected_sequential_sinks[0].cell->getPort(ID::Q);
		auto bad = f.run(request); require(!table_proof(bad) && !bad.constant_tables.empty() &&
			bad.constant_tables[0].reason_code == V02ReasonCode::SequentialBoundaryChanged, "hold/data snapshot change rejected");
	}
	{
		TableFixture f; auto good = f.run(); const auto *proof = table_proof(good); require(proof != nullptr, "domain anchor source proof exists");
		auto request = f.anchored(*proof);
		for (RTLIL::Cell *cell : f.module->cells())
			if (cell->type == ID($mux) && cell->getPort(ID::Y) == f.root) cell->setPort(ID::S, RTLIL::SigBit(f.address, 3));
		require(!table_proof(f.run(request)), "missing or repeated address domain rejected");
	}
	{
		TableFixture f;
		for (RTLIL::Cell *cell : f.module->cells()) if (cell->type == ID($mux) && cell->getPort(ID::A).is_fully_const()) {
			cell->setPort(ID::A, RTLIL::State::Sx); break;
		}
		require(!table_proof(f.run()), "x/z bit-sliced table leaf rejected");
	}
	{
		TableFixture f; RTLIL::Cell *driver = f.module->addCell(id("extra_driver"), ID($not));
		driver->setParam(ID::A_WIDTH, 1); driver->setParam(ID::A_SIGNED, 0); driver->setParam(ID::Y_WIDTH, 1);
		driver->setPort(ID::A, RTLIL::State::S0); driver->setPort(ID::Y, f.root[0]);
		require(!table_proof(f.run()), "multiple driver on bit-sliced table root rejected");
	}
	for (PmuxOptTestable::BudgetKind kind : {PmuxOptTestable::BudgetKind::Cells,
			PmuxOptTestable::BudgetKind::Bits, PmuxOptTestable::BudgetKind::Events}) {
		PmuxOptTestable::BudgetTracker tracker(1, 1, 1);
		bool first = kind == PmuxOptTestable::BudgetKind::Cells ? tracker.take_cell() :
			kind == PmuxOptTestable::BudgetKind::Bits ? tracker.take_bit() : tracker.take_event();
		bool second = kind == PmuxOptTestable::BudgetKind::Cells ? tracker.take_cell() :
			kind == PmuxOptTestable::BudgetKind::Bits ? tracker.take_bit() : tracker.take_event();
		require(first && !second && tracker.exhausted() == kind, "production proof budget boundary rejects");
	}
}

void run_tests()
{
	test_guarded(); test_table();
	log("PMUX production case34 selftest: all checks passed (%d assertions)\n", production_checks);
}

struct Runner { Runner() { run_tests(); } } runner;

} // namespace
} // namespace PmuxOpt

YOSYS_NAMESPACE_END
