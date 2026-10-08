#!/usr/bin/env python3
"""Role C: capture actual v0.2 insertion-point before/after nets for official cases."""
import argparse
import json
import os
import subprocess
from pathlib import Path
PROJECT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--bin", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    bin_dir, out = args.bin.resolve(), args.out.resolve()
    plugin = (PROJECT / "pass/pmux_opt.so").resolve()
    env = os.environ.copy()
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    for n in (3, 4):
        top = f"test{n}"
        d = out / "insertion" / top
        d.mkdir(parents=True, exist_ok=True)
        source = (PROJECT / "pmux_case/competition_case" / f"{top}.v").resolve()
        script = "\n".join([
            f'read_verilog "{source}"',
            f'synth_intel -family cycloneiv -top {top} -run begin:map_ffram',
            'opt -fast -mux_undef -undriven -fine -full',
            'memory_map',
            'opt -undriven -fine',
            f'write_json "{d / "before.json"}"',
            f'write_rtlil "{d / "before.il"}"',
            f'pmux_opt -v02-case34 -write-plan "{d / "plan.json"}" -write-stats "{d / "stats.json"}"',
            'check',
            f'write_json "{d / "after.json"}"',
            f'write_rtlil "{d / "after.il"}"',
            ''
        ])
        (d / "run.ys").write_text(script)
        argv = [str(bin_dir / "yosys"), "-Q", "-m", str(plugin), "-s", str(d / "run.ys")]
        (d / "command.json").write_text(json.dumps(argv, indent=2) + "\n")
        with (d / "yosys.log").open("w") as log:
            code = subprocess.run(argv, cwd=PROJECT.parent, env=env, stdout=log, stderr=subprocess.STDOUT).returncode
        if code:
            raise SystemExit(f"{top} insertion failed; see {d / 'yosys.log'}")
        if "Found and reported 0 problems." not in (d / "yosys.log").read_text():
            raise SystemExit(f"{top} check failed")
        print(f"{top} insertion/check PASS", flush=True)

if __name__ == "__main__":
    main()
