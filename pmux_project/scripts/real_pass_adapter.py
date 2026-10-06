#!/usr/bin/env python3
"""Run the integrated default pmux_opt and preserve its machine outputs verbatim."""
import argparse
import os
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ("before", "after", "plan", "stats", "plugin", "yosys"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    before, after, plan, stats = (Path(getattr(args, n)).resolve()
                                  for n in ("before", "after", "plan", "stats"))
    plugin = Path(args.plugin).resolve()
    yosys = Path(args.yosys).resolve()
    script = (f'read_rtlil "{before}"; plugin -i "{plugin}"; '
              f'pmux_opt -write-plan "{plan}" -write-stats "{stats}"; '
              f'write_rtlil "{after}"')
    work = after.parent
    work.mkdir(parents=True, exist_ok=True)
    (work / "real_pass.command.txt").write_text(str(yosys) + " -Q -p " + script + "\n")
    env = os.environ.copy()
    env["PATH"] = str(yosys.parent) + os.pathsep + env.get("PATH", "")
    process = subprocess.run([str(yosys), "-Q", "-p", script], cwd=work,
                             env=env, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
    (work / "real_pass.log").write_text(process.stdout)
    if process.returncode:
        print(process.stdout[-3000:], file=sys.stderr)
        raise SystemExit(process.returncode)
    if not all(p.is_file() for p in (after, plan, stats)):
        raise SystemExit("pmux_opt did not produce after.il, plan.json, stats.json")


if __name__ == "__main__":
    main()
