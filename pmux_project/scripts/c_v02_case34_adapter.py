#!/usr/bin/env python3
"""C's explicit v0.2 Pass adapter for the frozen random regression harness."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    for name in ("before", "after", "plan", "stats", "plugin", "yosys"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    before, after, plan, stats, plugin, yosys = (
        getattr(args, name).resolve()
        for name in ("before", "after", "plan", "stats", "plugin", "yosys")
    )
    after.parent.mkdir(parents=True, exist_ok=True)
    command = (
        f'read_rtlil "{before}"; plugin -i "{plugin}"; '
        f'pmux_opt -v02-case34 -write-plan "{plan}" -write-stats "{stats}"; '
        'hierarchy -check -top top; check; '
        f'write_rtlil "{after}"'
    )
    (after.parent / "c_v02_pass.command.txt").write_text(
        str(yosys) + " -Q -p " + command + "\n"
    )
    env = os.environ.copy()
    env["PATH"] = str(yosys.parent) + os.pathsep + env.get("PATH", "")
    run = subprocess.run(
        [str(yosys), "-Q", "-p", command], cwd=after.parent, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    (after.parent / "c_v02_pass.log").write_text(run.stdout)
    if run.returncode or "Found and reported 0 problems." not in run.stdout:
        print(run.stdout[-3000:], file=sys.stderr)
        raise SystemExit(run.returncode or 1)
    if not all(path.is_file() for path in (after, plan, stats)):
        raise SystemExit("missing Pass output")
    plan_data = json.loads(plan.read_text())
    stats_data = json.loads(stats.read_text())
    structural = plan_data.get("structural_edits", [])
    if (plan_data.get("mode") != "real" or stats_data.get("mode") != "real"
            or stats_data.get("v02_case34", {}).get("enabled") is not True
            or stats_data.get("changed_structures") != len(structural)):
        raise SystemExit("Pass machine contract mismatch")
    (after.parent / "c_v02_structural_summary.json").write_text(
        json.dumps({"structural_edits": structural,
                    "decision_cone_candidates": stats_data.get("v02_case34", {}).get(
                        "decision_cone_candidates"),
                    "changed_structures": stats_data.get("changed_structures")},
                   indent=2) + "\n"
    )

if __name__ == "__main__":
    main()