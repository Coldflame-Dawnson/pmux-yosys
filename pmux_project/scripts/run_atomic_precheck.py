#!/usr/bin/env python3
"""Exercise B's test-only snapshot abort; never count it as real proof."""
import argparse
import json
import os
import shutil
import sys
from pathlib import Path

from run_micro import DEFAULT_BIN, FIX, ROOT, compare, run, save, yosys_json
from run_random import eqy_config


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, default=DEFAULT_BIN)
    parser.add_argument("--plugin", type=Path, default=ROOT / "pass" / "pmux_opt.so")
    parser.add_argument("--out", type=Path, default=ROOT / "reports" / "c-real-atomic")
    args = parser.parse_args()
    work = args.out.resolve(); work.mkdir(parents=True, exist_ok=True)
    before, after = work / "before.il", work / "after.il"
    plan_file, stats_file = work / "plan.json", work / "stats.json"
    shutil.copyfile(FIX / "01_mux_a.il", before)
    yosys, eqy = args.bin / "yosys", args.bin / "eqy"
    env = os.environ.copy(); env["PATH"] = str(args.bin) + os.pathsep + env.get("PATH", "")
    script = (f'read_rtlil "{before}"; plugin -i "{args.plugin.resolve()}"; '
              f'pmux_opt -stub-one -test-snapshot-mismatch -write-plan "{plan_file}" '
              f'-write-stats "{stats_file}"; write_rtlil "{after}"')
    rc = run([yosys, "-Q", "-p", script], work / "dut.log", work, env)
    issues = []
    if rc: issues.append(f"DUT rc={rc}")
    if not all(p.exists() for p in (after, plan_file, stats_file)):
        issues.append("missing Pass artifact")
    result = {"schema": 1, "mode": "test-only-snapshot-abort", "status": "FAIL",
              "structure": "NOT_RUN", "check": "NOT_RUN", "eqy": "NOT_RUN", "issues": issues}
    if not issues:
        old = yosys_json(before, work / "before.json", yosys, work, env, "before_json")
        new = yosys_json(after, work / "after.json", yosys, work, env, "after_json")
        plan, stats = json.loads(plan_file.read_text()), json.loads(stats_file.read_text())
        diff_issues, count = compare(old, new, {"expected_edits": []}, plan, True)
        issues.extend(diff_issues)
        if old != new or count or plan.get("edits") or stats.get("changed_bits") != 0:
            issues.append("snapshot abort did not leave exact zero-write design")
        if plan.get("commit") != "aborted" or stats.get("rewrite", {}).get("commit") != "aborted":
            issues.append("machine commit status is not aborted")
        if plan.get("commit_reason") != "test_snapshot_mismatch" or "test_snapshot_mismatch" not in stats.get("reason_codes", []):
            issues.append("missing test-only snapshot mismatch reason")
        result["structure"] = "PASS" if not issues else "FAIL"
        check_script = f'read_rtlil "{after}"; hierarchy -check -top top; check'
        rc = run([yosys, "-Q", "-p", check_script], work / "check.log", work, env)
        result["check"] = "PASS" if rc == 0 and "0 problems" in (work / "check.log").read_text() else "FAIL"
        cfg = work / "verify.eqy"
        save(cfg, eqy_config(before, after, old["top"]))
        rc = run([eqy, "-f", "-d", work / "eqy_work", cfg], work / "eqy.log", work, env, timeout=180)
        result["eqy"] = "PASS" if rc == 0 and "DONE (PASS" in (work / "eqy.log").read_text() else "FAIL"
    result["issues"] = issues
    result["status"] = "PASS" if not issues and result["check"] == result["eqy"] == "PASS" else "FAIL"
    save(work / "summary.json", json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(result)
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
