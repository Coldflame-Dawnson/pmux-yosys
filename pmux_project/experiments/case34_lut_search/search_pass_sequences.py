#!/usr/bin/env python3
"""Search pass-order variants in the frozen case3/case4 synthesis window."""

import argparse
import json
import subprocess
from pathlib import Path


VARIANTS = {
	"baseline": ([], []),
	"opt_clean": (["opt_clean"], []),
	"opt_full": (["opt -full"], []),
	"opt_muxtree": (["opt_muxtree"], []),
	"opt_reduce": (["opt_reduce"], []),
	"opt_share": (["opt_share"], []),
	"muxpack": (["muxpack"], []),
	"pmuxtree": (["pmuxtree"], []),
	"muxtree_reduce_share": (["opt_muxtree", "opt_reduce", "opt_share", "opt_clean"], []),
	"share_muxtree_reduce": (["opt_share", "opt_muxtree", "opt_reduce", "opt_clean"], []),
	"reduce_share_full": (["opt_reduce", "opt_share", "opt -full"], []),
	"wreduce_peepopt": (["wreduce", "peepopt", "opt_clean"], []),
	"muxpack_muxtree": (["muxpack", "opt_muxtree", "opt_reduce", "opt_clean"], []),
	"pmuxtree_muxtree": (["pmuxtree", "opt_muxtree", "opt_reduce", "opt_clean"], []),
	"post_opt_share": ([], ["opt_share", "opt_clean"]),
	"post_muxtree": ([], ["opt_muxtree", "opt_reduce", "opt_clean"]),
}


def q(path: Path) -> str:
	return '"' + path.as_posix().replace('"', '\\"') + '"'


def count_types(path: Path, top: str) -> dict[str, int]:
	doc = json.loads(path.read_text(encoding="utf-8"))
	module = doc["modules"][top]
	counts: dict[str, int] = {}
	for cell in module.get("cells", {}).values():
		cell_type = cell["type"]
		counts[cell_type] = counts.get(cell_type, 0) + 1
	return counts


def run_variant(yosys: Path, source: Path, top: str, out: Path,
		name: str, pre: list[str], post: list[str]) -> dict:
	work = out / top / name
	work.mkdir(parents=True, exist_ok=True)
	netlist = work / "post_abc9.json"
	commands = [
		f"read_verilog {q(source)}",
		f"synth_intel -family cycloneiv -top {top} -run begin:map_ffram",
		"opt -fast -mux_undef -undriven -fine -full",
		"memory_map",
		"opt -undriven -fine",
	]
	commands.extend(pre)
	commands.extend([
		"techmap -map +/techmap.v",
		"opt -full",
		"clean -purge",
		"setundef -undriven -zero",
	])
	commands.extend(post)
	commands.extend([
		f"synth_intel -family cycloneiv -top {top} -run map_ffs:map_cells",
		"check",
		f"write_json {q(netlist)}",
	])
	script = "; ".join(commands)
	(work / "run.ys").write_text(";\n".join(commands) + ";\n", encoding="utf-8")
	process = subprocess.run([str(yosys), "-Q", "-p", script], text=True,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	(work / "console.log").write_text(process.stdout, encoding="utf-8")
	if process.returncode or not netlist.is_file():
		return {"variant": name, "status": "FAIL", "returncode": process.returncode}
	counts = count_types(netlist, top)
	return {
		"variant": name,
		"status": "PASS",
		"lut": counts.get("$lut", 0),
		"dffeas": counts.get("dffeas", 0),
		"cell_types": counts,
	}


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--yosys", type=Path, required=True)
	parser.add_argument("--case3", type=Path, required=True)
	parser.add_argument("--case4", type=Path, required=True)
	parser.add_argument("--out", type=Path, required=True)
	args = parser.parse_args()
	args.yosys = args.yosys.resolve()
	args.out = args.out.resolve()
	args.out.mkdir(parents=True, exist_ok=True)

	all_results = {}
	for top, source in (("test3", args.case3.resolve()), ("test4", args.case4.resolve())):
		rows = []
		for name, (pre, post) in VARIANTS.items():
			rows.append(run_variant(args.yosys, source, top, args.out, name, pre, post))
		all_results[top] = rows
		passed = [row for row in rows if row["status"] == "PASS"]
		best = min(row["lut"] for row in passed)
		print(f"{top}: best={best} LUT; " + ", ".join(
			row["variant"] for row in passed if row["lut"] == best))

	(args.out / "summary.json").write_text(json.dumps({
		"schema": 1,
		"flow": "official Verilog through frozen pre-common-techmap window to cycloneiv ABC9",
		"results": all_results,
	}, indent=2) + "\n", encoding="utf-8")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
