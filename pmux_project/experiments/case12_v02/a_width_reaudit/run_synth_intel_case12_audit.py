#!/usr/bin/env python3
"""A-audit runner: frozen production flow with explicit case12 plus default case34."""
import argparse
import importlib.util
import os
import subprocess
from pathlib import Path


def load_upstream(path):
    spec = importlib.util.spec_from_file_location("upstream_pmux_runner", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    for name in ("yosys", "plugin", "source", "plan", "stats", "json", "log", "script"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--top", required=True)
    parser.add_argument("--family", default="cycloneiv")
    parser.add_argument("--stage", choices=("post_abc9", "full"), required=True)
    parser.add_argument("--rtlil", type=Path)
    parser.add_argument("--cofactor-map", type=Path)
    parser.add_argument("--abc-script", type=Path)
    args = parser.parse_args()
    args.v02_case34 = False
    args.no_v02_case34 = False

    this = Path(__file__).resolve()
    repo = this.parents[4]
    upstream_path = repo / "pmux_project" / "pass" / "run_synth_intel_pmux.py"
    upstream = load_upstream(upstream_path)
    if args.cofactor_map is None:
        args.cofactor_map = repo / "pmux_project" / "pass" / "pmux_case34_lut_map.v"
    if args.abc_script is None:
        args.abc_script = repo / "pmux_project" / "pass" / "pmux_case34_abc.script"

    for attribute in ("yosys", "plugin", "source", "cofactor_map", "abc_script"):
        path = getattr(args, attribute).resolve()
        if not path.is_file():
            parser.error(f"missing {attribute}: {path}")
        setattr(args, attribute, path)
    for path in (args.plan, args.stats, args.json, args.log, args.script, args.rtlil):
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)

    script_text = upstream.build_script(args)
    old = "pmux_opt -write-plan"
    new = "pmux_opt -v02-case12 -write-plan"
    if script_text.count(old) != 1:
        raise RuntimeError("unexpected upstream PMUX command shape")
    script_text = script_text.replace(old, new, 1)
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
    args.log.with_suffix(args.log.suffix + ".console").write_text(process.stdout, encoding="utf-8")
    if process.returncode:
        raise RuntimeError(f"Yosys failed with rc={process.returncode}")
    upstream.require_real_outputs(args, args.log.read_text(encoding="utf-8", errors="replace"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
