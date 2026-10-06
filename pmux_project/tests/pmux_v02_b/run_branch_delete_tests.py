#!/usr/bin/env python3
"""Deterministic structural tests for B's v0.2 whole-branch rewriter."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def rtlil(s_width: int = 3, keep: bool = False) -> str:
	width = 2
	keep_attr = "  attribute \\keep 1\n" if keep else ""
	branches = " ".join(f"\\b{i}" for i in reversed(range(s_width)))
	return f"""autoidx 1
module \\top
  wire width {width} input 1 \\a
  wire width {width} input 2 \\b0
""" + "".join(
		f"  wire width {width} input {index + 2} \\b{index}\n"
		for index in range(1, s_width)
	) + f"""  wire width {s_width} input {s_width + 2} \\s
  wire width {width} output {s_width + 3} \\y
{keep_attr}  cell $pmux \\p
    parameter \\WIDTH {width}
    parameter \\S_WIDTH {s_width}
    connect \\A \\a
    connect \\B {{ {branches} }}
    connect \\S \\s
    connect \\Y \\y
  end
end
"""


def width(parameter: str) -> int:
	return int(parameter, 2)


def run_case(root: Path, yosys: Path, plugin: Path, name: str, proofs: list[int],
		extra: list[str] | None = None, s_width: int = 3, keep: bool = False,
		selection: str | None = None, enable_v02: bool = True) -> tuple[dict, dict, dict, str]:
	case = root / name
	case.mkdir(exist_ok=True)
	before = case / "before.il"
	before.write_text(rtlil(s_width=s_width, keep=keep), encoding="utf-8")
	plan = case / "plan.json"
	stats = case / "stats.json"
	after = case / "after.json"
	args = ["-v02-branch-delete"] if enable_v02 else []
	for branch in proofs:
		args.extend(["-test-branch-proof", "p", str(branch)])
	args.extend(extra or [])
	command = "pmux_opt " + " ".join(args)
	command += f" -write-plan {plan.as_posix()} -write-stats {stats.as_posix()}"
	if selection:
		command += " " + selection
	script = (
		f"read_rtlil {before.as_posix()}; {command}; check; "
		f"write_json {after.as_posix()}"
	)
	process = subprocess.run([str(yosys), "-Q", "-m", str(plugin), "-p", script],
		text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	(case / "console.log").write_text(process.stdout, encoding="utf-8")
	if process.returncode:
		raise RuntimeError(f"{name}: Yosys failed\n{process.stdout}")
	return (
		json.loads(after.read_text(encoding="utf-8")),
		json.loads(plan.read_text(encoding="utf-8")),
		json.loads(stats.read_text(encoding="utf-8")),
		process.stdout,
	)


def assert_deleted(after: dict, deleted: list[int], old_s_width: int = 3) -> None:
	module = after["modules"]["top"]
	remaining = [index for index in range(old_s_width) if index not in deleted]
	if not remaining:
		assert "p" not in module["cells"]
		assert module["netnames"]["y"]["bits"] == module["netnames"]["a"]["bits"]
		return
	cell = module["cells"]["p"]
	baseline_b = []
	for index in remaining:
		baseline_b.extend(module["netnames"][f"b{index}"]["bits"])
	assert cell["connections"]["B"] == baseline_b
	if len(remaining) == 1:
		assert cell["type"] == "$mux"
		assert "S_WIDTH" not in cell["parameters"]
	else:
		assert cell["type"] == "$pmux"
		assert width(cell["parameters"]["S_WIDTH"]) == len(remaining)


def assert_unchanged(after: dict, old_s_width: int = 3) -> None:
	cell = after["modules"]["top"]["cells"]["p"]
	assert cell["type"] == "$pmux"
	assert width(cell["parameters"]["S_WIDTH"]) == old_s_width


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--yosys", type=Path, required=True)
	parser.add_argument("--plugin", type=Path, required=True)
	parser.add_argument("--out", type=Path)
	parser.add_argument("--case3-before", type=Path)
	parser.add_argument("--case3-expected-json", type=Path)
	args = parser.parse_args()
	args.yosys = args.yosys.resolve()
	args.plugin = args.plugin.resolve()
	if not args.yosys.is_file() or not args.plugin.is_file():
		parser.error("--yosys and --plugin must be files")

	context = tempfile.TemporaryDirectory(prefix="pmux-v02-b-") if args.out is None else None
	root = Path(context.name) if context else args.out.resolve()
	root.mkdir(parents=True, exist_ok=True)
	results = []

	def execute(name: str, deleted: list[int], **kwargs):
		after, plan, stats, _ = run_case(root, args.yosys, args.plugin, name, deleted, **kwargs)
		assert plan["commit"] == "ok"
		assert_deleted(after, deleted, kwargs.get("s_width", 3))
		assert stats["branch_rewrite"]["committed_branches"] == len(set(deleted))
		results.append({"case": name, "result": "PASS"})

	execute("delete_first", [0])
	execute("delete_middle", [1])
	execute("delete_tail", [2])
	execute("multiple_to_mux", [0, 2])
	execute("all_to_a", [0, 1, 2])
	execute("single_to_a", [0], s_width=1)
	execute("v02_no_provider", [])
	execute("default_disabled", [], enable_v02=False)

	after, plan, stats, _ = run_case(root, args.yosys, args.plugin, "partial_rejected", [],
		extra=["-test-branch-partial", "p", "1"])
	assert_unchanged(after)
	assert plan["commit"] == "ok" and not plan["branch_edits"]
	assert stats["branch_rewrite"]["rejected_partial"] == 1
	results.append({"case": "partial_rejected", "result": "PASS"})

	for name, proofs, extra, reason in (
		("invalid_index", [7], [], "invalid_branch_rewrite_metadata"),
		("snapshot_mismatch", [1], ["-test-branch-snapshot-mismatch"], "test_branch_snapshot_mismatch"),
		("conflict", [1], ["-test-branch-conflict"], "branch_target_conflict"),
	):
		after, plan, _, _ = run_case(root, args.yosys, args.plugin, name, proofs, extra=extra)
		assert_unchanged(after)
		assert plan["commit"] == "aborted" and plan["commit_reason"] == reason
		assert not plan["branch_edits"]
		results.append({"case": name, "result": "PASS"})

	after, plan, stats, _ = run_case(root, args.yosys, args.plugin, "duplicate", [1],
		extra=["-test-branch-duplicate"])
	assert_deleted(after, [1])
	assert len(plan["branch_edits"]) == 1
	assert stats["branch_rewrite"]["deduplicated_targets"] == 1
	results.append({"case": "duplicate", "result": "PASS"})

	for name, kwargs in (
		("keep", {"proofs": [1], "keep": True}),
		("selection", {"proofs": [1], "selection": "w:a"}),
	):
		after, plan, stats, _ = run_case(root, args.yosys, args.plugin, name, **kwargs)
		assert_unchanged(after)
		assert not plan["branch_edits"]
		assert stats["branch_rewrite"]["committed_branches"] == 0
		results.append({"case": name, "result": "PASS"})

	if (args.case3_before is None) != (args.case3_expected_json is None):
		parser.error("--case3-before and --case3-expected-json must be supplied together")
	if args.case3_before is not None:
		case = root / "case3_v2_b_replay"
		case.mkdir(exist_ok=True)
		plan = case / "plan.json"
		stats = case / "stats.json"
		after = case / "after.json"
		script = (
			f"read_rtlil {args.case3_before.resolve().as_posix()}; "
			"pmux_opt -v02-branch-delete -test-branch-proof $procmux$18 0 "
			f"-write-plan {plan.as_posix()} -write-stats {stats.as_posix()}; "
			f"check; write_json {after.as_posix()}"
		)
		process = subprocess.run([str(args.yosys), "-Q", "-m", str(args.plugin), "-p", script],
			text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		(case / "console.log").write_text(process.stdout, encoding="utf-8")
		if process.returncode:
			raise RuntimeError(f"case3 replay: Yosys failed\n{process.stdout}")
		actual = json.loads(after.read_text(encoding="utf-8"))
		expected = json.loads(args.case3_expected_json.resolve().read_text(encoding="utf-8"))
		assert actual["modules"] == expected["modules"]
		plan_data = json.loads(plan.read_text(encoding="utf-8"))
		assert plan_data["commit"] == "ok"
		assert plan_data["branch_edits"][0]["transformation"] == "pmux_to_mux"
		results.append({"case": "case3_v2_b_replay", "result": "PASS"})

	(root / "summary.json").write_text(json.dumps({"schema": 1, "tests": results}, indent=2) + "\n",
		encoding="utf-8")
	print(f"pmux v0.2 B structural tests: {len(results)}/{len(results)} PASS")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
