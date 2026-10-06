#!/usr/bin/env python3
"""Run the frozen synth_intel flow with pmux_opt at the audited insertion point."""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def ys_quote(path: Path) -> str:
	return '"' + str(path).replace('\\', '/').replace('"', '\\"') + '"'


def build_script(args) -> str:
	commands = [
		f"read_verilog {ys_quote(args.source)}",
		f"synth_intel -family {args.family} -top {args.top} -run begin:map_ffram",
		"opt -fast -mux_undef -undriven -fine -full",
		"memory_map",
		"opt -undriven -fine",
		f"pmux_opt -write-plan {ys_quote(args.plan)} -write-stats {ys_quote(args.stats)}",
		"opt_clean",
		"techmap -map +/techmap.v",
		"opt -full",
		"clean -purge",
		"setundef -undriven -zero",
	]
	if args.stage == "post_abc9":
		commands.append(f"synth_intel -family {args.family} -top {args.top} -run map_ffs:map_cells")
	else:
		commands.append(f"synth_intel -family {args.family} -top {args.top} -run map_ffs:")
	commands.extend([
		"hierarchy -check",
		"check -noinit",
		"stat",
		f"write_json {ys_quote(args.json)}",
	])
	if args.rtlil is not None:
		commands.extend([
			f"select {args.top}",
			f"write_rtlil -selected {ys_quote(args.rtlil)}",
		])
	return ";\n".join(commands) + ";\n"


def require_real_outputs(args, log_text: str) -> None:
	marker = "Executing PMUX_OPT guarded analysis and atomic rewrite."
	if log_text.count(marker) != 1:
		raise RuntimeError(f"expected exactly one real PMUX_OPT execution, found {log_text.count(marker)}")

	pmux_pos = log_text.index(marker)
	memory_pos = log_text.rfind("Executing MEMORY_MAP pass", 0, pmux_pos)
	clean_pos = log_text.find("Executing OPT_CLEAN pass", pmux_pos)
	techmap_pos = log_text.find("Executing TECHMAP pass", pmux_pos)
	abc9_pos = log_text.find("Executing ABC9 pass", pmux_pos)
	if memory_pos < 0 or clean_pos < 0 or techmap_pos < 0 or abc9_pos < 0:
		raise RuntimeError("missing MEMORY_MAP, post-PMUX OPT_CLEAN/TECHMAP, or ABC9 marker")
	if not memory_pos < pmux_pos < clean_pos < techmap_pos < abc9_pos:
		raise RuntimeError("PMUX_OPT did not run in the frozen pre-common-techmap window")

	for path in (args.plan, args.stats, args.json):
		if not path.is_file():
			raise RuntimeError(f"missing expected output: {path}")
	if args.rtlil is not None and not args.rtlil.is_file():
		raise RuntimeError(f"missing expected output: {args.rtlil}")

	plan = json.loads(args.plan.read_text(encoding="utf-8"))
	stats = json.loads(args.stats.read_text(encoding="utf-8"))
	if plan.get("mode") != "real" or stats.get("mode") != "real":
		raise RuntimeError("pmux_opt outputs are not in real mode")
	if stats.get("changed_bits") != len(plan.get("edits", [])):
		raise RuntimeError("changed_bits differs from committed plan edit count")


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--yosys", type=Path, required=True)
	parser.add_argument("--plugin", type=Path, required=True)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--top", required=True)
	parser.add_argument("--family", default="cycloneiv")
	parser.add_argument("--stage", choices=("post_abc9", "full"), required=True)
	parser.add_argument("--plan", type=Path, required=True)
	parser.add_argument("--stats", type=Path, required=True)
	parser.add_argument("--json", type=Path, required=True)
	parser.add_argument("--rtlil", type=Path)
	parser.add_argument("--log", type=Path, required=True)
	parser.add_argument("--script", type=Path, required=True)
	args = parser.parse_args()

	for path, label in ((args.yosys, "Yosys"), (args.plugin, "plugin"), (args.source, "source")):
		path = path.resolve()
		if not path.is_file():
			parser.error(f"missing {label}: {path}")
		if label == "Yosys":
			args.yosys = path
		elif label == "plugin":
			args.plugin = path
		else:
			args.source = path

	for path in (args.plan, args.stats, args.json, args.log, args.script, args.rtlil):
		if path is not None:
			path.parent.mkdir(parents=True, exist_ok=True)

	script_text = build_script(args)
	args.script.write_text(script_text, encoding="utf-8")
	env = os.environ.copy()
	env["PATH"] = str(args.yosys.parent) + os.pathsep + env.get("PATH", "")
	process = subprocess.run(
		[str(args.yosys), "-m", str(args.plugin), "-l", str(args.log), "-s", str(args.script)],
		text=True,
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		env=env,
	)
	console = args.log.with_suffix(args.log.suffix + ".console")
	console.write_text(process.stdout, encoding="utf-8")
	if process.returncode != 0:
		print(process.stdout, file=sys.stderr)
		raise RuntimeError(f"Yosys failed with rc={process.returncode}")

	log_text = args.log.read_text(encoding="utf-8", errors="replace")
	require_real_outputs(args, log_text)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
