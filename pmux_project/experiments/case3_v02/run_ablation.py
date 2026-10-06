#!/usr/bin/env python3
"""Generate five exact-snapshot case3 structural ablation variants."""

import argparse
import hashlib
import json
import os
import shlex
import shutil
import subprocess
from pathlib import Path


VARIANTS = ("a", "b", "c", "ab", "abc")


def sha256(path: Path) -> str:
	return hashlib.sha256(path.read_bytes()).hexdigest()


def quote(path: Path) -> str:
	return '"' + str(path).replace('\\', '/').replace('"', '\\"') + '"'


def cell(module: dict, name: str) -> dict:
	try:
		return module["cells"][name]
	except KeyError as error:
		raise RuntimeError(f"missing cell {name}") from error


def assert_structure(variant: str, after_json: Path) -> None:
	data = json.loads(after_json.read_text(encoding="utf-8"))
	module = data["modules"]["test3"]
	cells = module["cells"]
	nets = module["netnames"]
	sr = nets["sr"]["bits"]

	if "a" in variant:
		if "$procmux$10" in cells:
			raise RuntimeError(f"variant {variant}: A target was not deleted")
		if nets["$procmux$10_Y"]["bits"] != [sr[6], sr[5], sr[4]]:
			raise RuntimeError(f"variant {variant}: A bypass connection is wrong")
	else:
		if cell(module, "$procmux$10")["type"] != "$mux":
			raise RuntimeError(f"variant {variant}: A target changed unexpectedly")

	b_target = cell(module, "$procmux$18")
	if "b" in variant:
		if b_target["type"] != "$mux" or int(b_target["parameters"]["WIDTH"], 2) != 1:
			raise RuntimeError(f"variant {variant}: B target was not reduced to WIDTH=1 $mux")
		if "S_WIDTH" in b_target["parameters"]:
			raise RuntimeError(f"variant {variant}: B target retained S_WIDTH")
		if b_target["connections"]["B"] != ["1"]:
			raise RuntimeError(f"variant {variant}: B did not retain original branch 1")
	else:
		if b_target["type"] != "$pmux" or int(b_target["parameters"]["S_WIDTH"], 2) != 2:
			raise RuntimeError(f"variant {variant}: B target changed unexpectedly")

	c_target = cell(module, "$procmux$27")
	if "c" in variant:
		if c_target["type"] != "$mux" or int(c_target["parameters"]["WIDTH"], 2) != 1:
			raise RuntimeError(f"variant {variant}: C target was not reduced to WIDTH=1 $mux")
		if nets["$procmux$27_Y"]["bits"][:3] != [sr[3], sr[2], sr[1]]:
			raise RuntimeError(f"variant {variant}: C bypass connection is wrong")
	else:
		if int(c_target["parameters"]["WIDTH"], 2) != 4:
			raise RuntimeError(f"variant {variant}: C target changed unexpectedly")


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--yosys", type=Path, required=True)
	parser.add_argument("--plugin", type=Path, required=True)
	parser.add_argument("--before", type=Path, required=True)
	parser.add_argument("--out", type=Path, required=True)
	args = parser.parse_args()
	args.yosys = args.yosys.resolve()
	args.plugin = args.plugin.resolve()
	args.before = args.before.resolve()
	args.out = args.out.resolve()
	for path, label in ((args.yosys, "yosys"), (args.plugin, "plugin"), (args.before, "before")):
		if not path.is_file():
			parser.error(f"missing {label}: {path}")
	args.out.mkdir(parents=True, exist_ok=True)

	env = os.environ.copy()
	env["PATH"] = str(args.yosys.parent) + os.pathsep + env.get("PATH", "")
	rows = []
	for variant in VARIANTS:
		work = args.out / variant
		work.mkdir(parents=True, exist_ok=True)
		before = work / "before.il"
		after = work / "after.il"
		after_json = work / "after.json"
		manifest = work / "manifest.json"
		log = work / "yosys.log"
		shutil.copyfile(args.before, before)
		script = (
			f"read_rtlil {quote(before)}; hierarchy -check -top test3; "
			f"case3_v02_ablate -variant {variant} -write-manifest {quote(manifest)}; "
			f"check; write_rtlil {quote(after)}; write_json {quote(after_json)}"
		)
		command = [str(args.yosys), "-Q", "-m", str(args.plugin), "-l", str(log), "-p", script]
		(work / "command.txt").write_text(shlex.join(command) + "\n", encoding="utf-8")
		process = subprocess.run(command, text=True, stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, env=env)
		(work / "console.log").write_text(process.stdout, encoding="utf-8")
		if process.returncode:
			raise RuntimeError(f"variant {variant}: Yosys rc={process.returncode}")
		text = log.read_text(encoding="utf-8", errors="replace")
		if "Found and reported 0 problems" not in text:
			raise RuntimeError(f"variant {variant}: Yosys check did not report zero problems")
		data = json.loads(manifest.read_text(encoding="utf-8"))
		if data.get("variant") != variant or data.get("snapshot_check") != "passed":
			raise RuntimeError(f"variant {variant}: invalid manifest")
		assert_structure(variant, after_json)
		rows.append({
			"variant": variant,
			"actions": data["actions"],
			"before_sha256": sha256(before),
			"after_sha256": sha256(after),
			"json_sha256": sha256(after_json),
			"yosys_check": "PASS",
			"structural_assertions": "PASS",
		})

	negative = args.out / "negative_snapshot"
	negative.mkdir(parents=True, exist_ok=True)
	negative_manifest = negative / "manifest.json"
	negative_after = negative / "after.il"
	for stale in (negative_manifest, negative_after):
		if stale.exists():
			stale.unlink()
	negative_log = negative / "yosys.log"
	negative_script = (
		f"read_rtlil {quote(args.out / 'a' / 'after.il')}; hierarchy -check -top test3; "
		f"case3_v02_ablate -variant a -write-manifest {quote(negative_manifest)}; "
		f"write_rtlil {quote(negative_after)}"
	)
	negative_command = [str(args.yosys), "-Q", "-m", str(args.plugin), "-l",
		str(negative_log), "-p", negative_script]
	(negative / "command.txt").write_text(shlex.join(negative_command) + "\n", encoding="utf-8")
	negative_process = subprocess.run(negative_command, text=True, stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT, env=env)
	(negative / "console.log").write_text(negative_process.stdout, encoding="utf-8")
	negative_text = negative_log.read_text(encoding="utf-8", errors="replace")
	if negative_process.returncode == 0 or "snapshot mismatch: missing cell $procmux$10" not in negative_text:
		raise RuntimeError("negative snapshot test did not fail for the expected reason")
	if negative_manifest.exists() or negative_after.exists():
		raise RuntimeError("negative snapshot test produced output after validation failure")

	summary = {
		"schema": 1,
		"mode": "test-only-case3-ablation",
		"production": False,
		"input_sha256": sha256(args.before),
		"variants": rows,
		"negative_snapshot": {
			"result": "PASS",
			"returncode_nonzero": True,
			"reason": "missing cell $procmux$10",
			"manifest_written": False,
			"after_written": False,
		},
	}
	(args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
