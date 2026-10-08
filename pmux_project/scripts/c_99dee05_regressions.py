#!/usr/bin/env python3
"""Role C: fresh micro, atomic and ordered fixed-seed real regressions."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]

def run(argv, log, env):
    log.parent.mkdir(parents=True, exist_ok=True)
    (log.parent / (log.stem + ".command.json")).write_text(json.dumps(argv, indent=2) + "\n")
    with log.open("w") as fp:
        code = subprocess.run(argv, cwd=PROJECT.parent, env=env, stdout=fp, stderr=subprocess.STDOUT).returncode
    if code:
        raise SystemExit(f"FAIL {log}: rc={code}; see complete log")
    print(f"PASS {log}", flush=True)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--bin", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args()
    bin_dir = args.bin.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    plugin = PROJECT / "pass/pmux_opt.so"
    def template(adapter):
        return (f"{sys.executable} {PROJECT / 'scripts' / adapter} "
                "--before {before} --after {after} --plan {plan} --stats {stats} "
                f"--plugin {plugin} --yosys {bin_dir / 'yosys'}")
    default = template("real_pass_adapter.py")
    explicit = template("c_v02_case34_adapter.py")
    run([sys.executable, str(PROJECT / "scripts/run_micro.py"), "--mode", "external",
         "--external-kind", "real", "--dut-template", default,
         "--bin", str(bin_dir), "--out", str(out / "micro")], out / "micro_driver.log", env)
    run([sys.executable, str(PROJECT / "scripts/run_atomic_precheck.py"),
         "--bin", str(bin_dir), "--plugin", str(plugin), "--out", str(out / "atomic")],
        out / "atomic_driver.log", env)
    for mode, dut in (("random_default", default), ("random_v02", explicit)):
        run([sys.executable, str(PROJECT / "scripts/run_random.py"), "--count", "1000",
             "--mode", "external", "--external-kind", "real", "--dut-template", dut,
             "--bin", str(bin_dir), "--out", str(out / mode)],
            out / (mode + "_driver.log"), env)

if __name__ == "__main__":
    main()
