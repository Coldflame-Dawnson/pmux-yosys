#!/usr/bin/env python3
"""A-audit adapter: run only the explicit v0.2 case1/case2 production path."""
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
    before, after, plan, stats = (
        Path(getattr(args, name)).resolve()
        for name in ("before", "after", "plan", "stats")
    )
    plugin = Path(args.plugin).resolve()
    yosys = Path(args.yosys).resolve()
    script = (
        f'read_rtlil "{before}"; plugin -i "{plugin}"; '
        f'pmux_opt -v02-case12 -no-v02-case34 '
        f'-write-plan "{plan}" -write-stats "{stats}"; '
        f'write_rtlil "{after}"'
    )
    after.parent.mkdir(parents=True, exist_ok=True)
    (after.parent / "explicit_case12.command.txt").write_text(
        str(yosys) + " -Q -p " + script + "\n", encoding="utf-8"
    )
    env = os.environ.copy()
    env["PATH"] = str(yosys.parent) + os.pathsep + env.get("PATH", "")
    process = subprocess.run(
        [str(yosys), "-Q", "-p", script],
        cwd=after.parent,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    (after.parent / "explicit_case12.log").write_text(process.stdout, encoding="utf-8")
    if process.returncode:
        print(process.stdout[-3000:], file=sys.stderr)
        raise SystemExit(process.returncode)
    if not all(path.is_file() for path in (after, plan, stats)):
        raise SystemExit("pmux_opt did not produce all requested machine outputs")


if __name__ == "__main__":
    main()
