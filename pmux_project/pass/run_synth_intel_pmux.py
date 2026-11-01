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
	pmux_command = f"pmux_opt -write-plan {ys_quote(args.plan)} -write-stats {ys_quote(args.stats)}"
	use_v02_case34 = not args.no_v02_case34
	if not use_v02_case34:
		pmux_command += " -no-v02-case34"
	if args.no_v02_case12 or args.family != "cycloneiv":
		pmux_command += " -no-v02-case12"
	commands = [
		f"read_verilog {ys_quote(args.source)}",
		f"synth_intel -family {args.family} -top {args.top} -run begin:map_ffram",
		"opt -fast -mux_undef -undriven -fine -full",
		"memory_map",
		"opt -undriven -fine",
		pmux_command,
		"opt_clean",
		"techmap -map +/techmap.v",
	]
	commands.extend([
		"opt -full",
		"clean -purge",
		"setundef -undriven -zero",
	])
	if args.stage == "post_abc9":
		commands.append(
			f"pmux_opt_abc9 -family {args.family} -top {args.top} "
			f"-cofactor-map {ys_quote(args.cofactor_map)} -abc-script {ys_quote(args.abc_script)}"
		)
	else:
		commands.extend([
			f"pmux_opt_abc9 -family {args.family} -top {args.top} "
			f"-cofactor-map {ys_quote(args.cofactor_map)} -abc-script {ys_quote(args.abc_script)}",
			f"synth_intel -family {args.family} -top {args.top} -run map_cells:",
		])
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
	if stats.get("changed_structures", 0) != len(plan.get("structural_edits", [])):
		raise RuntimeError("changed_structures differs from committed structural edit count")
	if stats.get("v02_case34", {}).get("enabled") != (not args.no_v02_case34):
		raise RuntimeError("v02_case34 mode differs from the requested runner mode")
	constant_table = any(edit.get("rule") == "constant_table_cofactor"
	                     for edit in plan.get("structural_edits", []))
	expected_profile = "accepted_constant_table" if constant_table else "stock"
	profile_marker = f"pmux_opt_abc9: profile={expected_profile}"
	if log_text.count(profile_marker) != 1:
		raise RuntimeError(f"expected exactly one structural ABC9 profile marker: {profile_marker}")


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--yosys", type=Path, required=True)
	parser.add_argument("--plugin", type=Path, required=True)
	parser.add_argument("--source", type=Path, required=True)
	parser.add_argument("--top", required=True)
	parser.add_argument("--family", default="cycloneiv")
	parser.add_argument("--stage", choices=("post_abc9", "full"), required=True)
	mode = parser.add_mutually_exclusive_group()
	mode.add_argument("--v02-case34", action="store_true",
	                  help="compatibility spelling; v0.2 production selection is already the default")
	mode.add_argument("--no-v02-case34", action="store_true",
	                  help="disable guarded-cone/constant-table rules only")
	parser.add_argument("--no-v02-case12", action="store_true",
	                    help="disable paired-PMUX/priority-chain rules only")
	parser.add_argument("--no-pmux-v02", action="store_true",
	                    help="disable all v0.2 structural families for baseline diagnostics")
	parser.add_argument("--cofactor-map", type=Path)
	parser.add_argument("--abc-script", type=Path)
	parser.add_argument("--plan", type=Path, required=True)
	parser.add_argument("--stats", type=Path, required=True)
	parser.add_argument("--json", type=Path, required=True)
	parser.add_argument("--rtlil", type=Path)
	parser.add_argument("--log", type=Path, required=True)
	parser.add_argument("--script", type=Path, required=True)
	args = parser.parse_args()
	if args.no_pmux_v02:
		if args.v02_case34:
			parser.error("--no-pmux-v02 conflicts with --v02-case34")
		args.no_v02_case12 = True
		args.no_v02_case34 = True
	if args.cofactor_map is None:
		args.cofactor_map = Path(__file__).resolve().with_name("pmux_case34_lut_map.v")
	if args.abc_script is None:
		args.abc_script = Path(__file__).resolve().with_name("pmux_case34_abc.script")

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
	for attribute, label in (("cofactor_map", "cofactor techmap"), ("abc_script", "ABC script")):
		path = getattr(args, attribute)
		if path is not None:
			path = path.resolve()
			if not path.is_file():
				parser.error(f"missing {label}: {path}")
			setattr(args, attribute, path)

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
