#include "kernel/yosys.h"

#include <fstream>
#include <string>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct TargetSnapshot
{
	RTLIL::Cell *cell = nullptr;
	RTLIL::SigSpec a, b, s, y;
};

static void require_snapshot(bool condition, const char *message)
{
	if (!condition)
		log_cmd_error("case3_v02_ablate: snapshot mismatch: %s\n", message);
}

static RTLIL::SigSpec make_sig(std::initializer_list<RTLIL::SigBit> bits)
{
	RTLIL::SigSpec result;
	for (RTLIL::SigBit bit : bits)
		result.append(bit);
	return result;
}

static RTLIL::Wire *require_wire(RTLIL::Module *module, const std::string &name, int width)
{
	RTLIL::Wire *wire = module->wire(RTLIL::IdString(name));
	require_snapshot(wire != nullptr, ("missing wire " + name).c_str());
	require_snapshot(GetSize(wire) == width, ("wrong width for wire " + name).c_str());
	return wire;
}

static RTLIL::Cell *require_cell(RTLIL::Module *module, const std::string &name,
		RTLIL::IdString type, int width)
{
	RTLIL::Cell *cell = module->cell(RTLIL::IdString(name));
	require_snapshot(cell != nullptr, ("missing cell " + name).c_str());
	require_snapshot(cell->type == type, ("wrong type for cell " + name).c_str());
	require_snapshot(cell->hasParam(ID::WIDTH) && cell->getParam(ID::WIDTH).as_int() == width,
			("wrong WIDTH for cell " + name).c_str());
	for (RTLIL::IdString port : {ID::A, ID::B, ID::S, ID::Y})
		require_snapshot(cell->hasPort(port), ("missing port on cell " + name).c_str());
	return cell;
}

static TargetSnapshot validate_a(RTLIL::Module *module, RTLIL::Wire *sr)
{
	TargetSnapshot snap;
	snap.cell = require_cell(module, "$procmux$10", ID($mux), 3);
	RTLIL::Wire *sel = require_wire(module, "$procmux$11_CMP", 1);
	RTLIL::Wire *out = require_wire(module, "$procmux$10_Y", 3);
	snap.a = snap.cell->getPort(ID::A);
	snap.b = snap.cell->getPort(ID::B);
	snap.s = snap.cell->getPort(ID::S);
	snap.y = snap.cell->getPort(ID::Y);
	require_snapshot(snap.a == make_sig({RTLIL::SigBit(sr, 6), RTLIL::SigBit(sr, 5), RTLIL::SigBit(sr, 4)}),
			"$procmux$10.A");
	require_snapshot(snap.b == make_sig({RTLIL::SigBit(sr, 2), RTLIL::SigBit(sr, 1), RTLIL::SigBit(sr, 0)}),
			"$procmux$10.B");
	require_snapshot(snap.s == RTLIL::SigSpec(sel), "$procmux$10.S");
	require_snapshot(snap.y == RTLIL::SigSpec(out), "$procmux$10.Y");
	return snap;
}

static TargetSnapshot validate_b(RTLIL::Module *module, RTLIL::Wire *sr)
{
	TargetSnapshot snap;
	snap.cell = require_cell(module, "$procmux$18", ID($pmux), 1);
	require_snapshot(snap.cell->hasParam(ID::S_WIDTH) &&
			snap.cell->getParam(ID::S_WIDTH).as_int() == 2, "$procmux$18.S_WIDTH");
	RTLIL::Wire *sel0 = require_wire(module, "$procmux$11_CMP", 1);
	RTLIL::Wire *sel1 = require_wire(module, "$procmux$12_CMP", 1);
	RTLIL::Wire *out = require_wire(module, "$procmux$18_Y", 1);
	snap.a = snap.cell->getPort(ID::A);
	snap.b = snap.cell->getPort(ID::B);
	snap.s = snap.cell->getPort(ID::S);
	snap.y = snap.cell->getPort(ID::Y);
	require_snapshot(snap.a == RTLIL::SigSpec(RTLIL::SigBit(sr, 0)), "$procmux$18.A");
	require_snapshot(snap.b == make_sig({RTLIL::SigBit(sr, 3), RTLIL::SigBit(RTLIL::State::S1)}),
			"$procmux$18.B");
	require_snapshot(snap.s == make_sig({RTLIL::SigBit(sel0, 0), RTLIL::SigBit(sel1, 0)}),
			"$procmux$18.S");
	require_snapshot(snap.y == RTLIL::SigSpec(out), "$procmux$18.Y");
	return snap;
}

static TargetSnapshot validate_c(RTLIL::Module *module, RTLIL::Wire *sr)
{
	TargetSnapshot snap;
	snap.cell = require_cell(module, "$procmux$27", ID($mux), 4);
	RTLIL::Wire *sel = require_wire(module, "$procmux$19_CMP", 1);
	RTLIL::Wire *out = require_wire(module, "$procmux$27_Y", 4);
	snap.a = snap.cell->getPort(ID::A);
	snap.b = snap.cell->getPort(ID::B);
	snap.s = snap.cell->getPort(ID::S);
	snap.y = snap.cell->getPort(ID::Y);
	require_snapshot(snap.a == make_sig({RTLIL::SigBit(sr, 3), RTLIL::SigBit(sr, 2),
			RTLIL::SigBit(sr, 1), RTLIL::SigBit(sr, 0)}), "$procmux$27.A");
	require_snapshot(snap.b == make_sig({RTLIL::SigBit(RTLIL::State::S1), RTLIL::SigBit(sr, 6),
			RTLIL::SigBit(sr, 5), RTLIL::SigBit(sr, 4)}), "$procmux$27.B");
	require_snapshot(snap.s == RTLIL::SigSpec(sel), "$procmux$27.S");
	require_snapshot(snap.y == RTLIL::SigSpec(out), "$procmux$27.Y");
	return snap;
}

static void apply_a(RTLIL::Module *module, const TargetSnapshot &snap)
{
	module->connect(snap.y, snap.a);
	module->remove(snap.cell);
}

static void apply_b(const TargetSnapshot &snap)
{
	snap.cell->type = ID($mux);
	snap.cell->parameters.erase(ID::S_WIDTH);
	snap.cell->setParam(ID::WIDTH, 1);
	snap.cell->setPort(ID::A, snap.a);
	snap.cell->setPort(ID::B, snap.b.extract(1, 1));
	snap.cell->setPort(ID::S, snap.s.extract(1, 1));
	snap.cell->setPort(ID::Y, snap.y);
}

static void apply_c(RTLIL::Module *module, const TargetSnapshot &snap)
{
	module->connect(snap.y.extract(0, 3), snap.a.extract(0, 3));
	snap.cell->setParam(ID::WIDTH, 1);
	snap.cell->setPort(ID::A, snap.a.extract(3, 1));
	snap.cell->setPort(ID::B, snap.b.extract(3, 1));
	snap.cell->setPort(ID::S, snap.s);
	snap.cell->setPort(ID::Y, snap.y.extract(3, 1));
}

static void write_manifest(std::string path, const std::string &variant,
		bool do_a, bool do_b, bool do_c)
{
	rewrite_filename(path);
	std::ofstream file(path);
	if (!file)
		log_cmd_error("case3_v02_ablate: cannot open manifest %s\n", path.c_str());
	file << "{\n  \"schema\": 1,\n  \"mode\": \"test-only-case3-ablation\",\n";
	file << "  \"variant\": \"" << variant << "\",\n  \"actions\": [";
	bool first = true;
	auto begin_action = [&]() {
		if (!first) file << ",";
		file << "\n    ";
		first = false;
	};
	if (do_a) {
		begin_action();
		file << "{\"id\": \"A\", \"cell\": \"$procmux$10\", "
				 "\"kind\": \"whole_mux_bypass\", \"deleted_cell\": true, "
				 "\"reconnect\": \"Y[2:0]=A[2:0]\"}";
	}
	if (do_b) {
		begin_action();
		file << "{\"id\": \"B\", \"cell\": \"$procmux$18\", "
				 "\"kind\": \"pmux_branch0_delete_to_mux\", "
				 "\"old_type\": \"$pmux\", \"new_type\": \"$mux\", "
				 "\"old_s_width\": 2, \"new_s_width\": 1, "
				 "\"removed_branch\": 0, \"retained_branch\": 1}";
	}
	if (do_c) {
		begin_action();
		file << "{\"id\": \"C\", \"cell\": \"$procmux$27\", "
				 "\"kind\": \"partial_bits_0_2_bypass\", "
				 "\"old_width\": 4, \"new_width\": 1, "
				 "\"bypassed_bits\": [0, 1, 2], \"retained_bits\": [3], "
				 "\"reconnect\": \"Y[2:0]=A[2:0]\"}";
	}
	file << "\n  ],\n  \"snapshot_check\": \"passed\",\n  \"production\": false\n}\n";
	if (!file)
		log_cmd_error("case3_v02_ablate: failed writing manifest %s\n", path.c_str());
}

struct Case3V02AblatePass : public Pass
{
	Case3V02AblatePass() : Pass("case3_v02_ablate", "test-only case3 v0.2 structural ablation") { }

	void help() override
	{
		log("    case3_v02_ablate -variant a|b|c|ab|abc -write-manifest <file>\n");
		log("\nTest-only exact-snapshot transformations for the frozen official case3.\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		std::string variant, manifest;
		std::size_t argidx = 1;
		for (; argidx < args.size(); ++argidx) {
			if (args[argidx] == "-variant" && argidx + 1 < args.size()) {
				variant = args[++argidx];
				continue;
			}
			if (args[argidx] == "-write-manifest" && argidx + 1 < args.size()) {
				manifest = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);
		const bool do_a = variant == "a" || variant == "ab" || variant == "abc";
		const bool do_b = variant == "b" || variant == "ab" || variant == "abc";
		const bool do_c = variant == "c" || variant == "abc";
		if ((!do_a && !do_b && !do_c) || manifest.empty())
			log_cmd_error("case3_v02_ablate: require -variant a|b|c|ab|abc and -write-manifest\n");

		RTLIL::Module *module = design->module(ID(test3));
		require_snapshot(module != nullptr, "missing module test3");
		require_snapshot(design->selected_whole_module(module), "test3 must be wholly selected");
		RTLIL::Wire *sr = require_wire(module, "\\sr", 8);

		// Validate every requested action before the first mutation.
		TargetSnapshot a, b, c;
		if (do_a) a = validate_a(module, sr);
		if (do_b) b = validate_b(module, sr);
		if (do_c) c = validate_c(module, sr);

		if (do_a) apply_a(module, a);
		if (do_b) apply_b(b);
		if (do_c) apply_c(module, c);
		write_manifest(manifest, variant, do_a, do_b, do_c);
		log("case3_v02_ablate: variant=%s actions=%d snapshot=passed production=false\n",
				variant.c_str(), int(do_a) + int(do_b) + int(do_c));
	}
} Case3V02AblatePass;

PRIVATE_NAMESPACE_END
