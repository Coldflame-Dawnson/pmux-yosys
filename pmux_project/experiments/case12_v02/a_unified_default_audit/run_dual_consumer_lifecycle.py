#!/usr/bin/env python3
"""A-side symmetric lifecycle audit for native synth and runner consumers."""

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
MODES = (
    "none", "lut_bit", "lut_width", "port", "attribute", "type",
    "remove_one", "add_extra", "same_name_rebuild", "drop_snapshot",
    "forged_generation", "unregister_monitor",
)


def run(command, *, log, env=None):
    completed = subprocess.run(
        [str(x) for x in command], cwd=ROOT, env=env, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
    )
    log.write_text(completed.stdout, encoding="utf-8")
    return completed


def profile(text, marker):
    found = re.findall(marker + r"(\w+)", text)
    return found[-1] if found else "missing"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool-root", type=Path, required=True)
    ap.add_argument("--official", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    tool = args.tool_root.resolve()
    official = args.official.resolve()
    out = args.output.resolve()
    build, runtime, logs = out / "build", out / "runtime", out / "logs"
    for directory in (build, runtime / "bin", runtime / "share", logs):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tool / "bin/yosys", runtime / "bin/yosys")
    shutil.copy2(tool / "bin/yosys-abc", runtime / "bin/yosys-abc")
    shutil.copytree(tool / "share/yosys", runtime / "share/yosys", dirs_exist_ok=True)
    common = runtime / "share/yosys/intel/common"
    for name in ("pmux_case34_lut_map.v", "pmux_case34_abc.script"):
        shutil.copy2(ROOT / "techlibs/intel/common" / name, common / name)

    yc = tool / "bin/yosys-config"
    native = build / "native_synth.so"
    mutation = build / "a_lifecycle_mutation.so"
    for name, target, sources in (
        ("native", native, [ROOT / "pmux_project/experiments/case34_v02b/scripts/native_synth_loader.cc"]),
        ("mutation", mutation, [HERE / "lifecycle_mutation_plugin.cc"]),
    ):
        result = run([yc, "--build", target, *sources, f"-I{ROOT}"],
                     log=logs / f"build_{name}.log")
        if result.returncode:
            raise SystemExit(f"build failed: {name}")

    yosys = runtime / "bin/yosys"
    plugin = ROOT / "pmux_project/pass/pmux_opt.so"
    base = [yosys, "-Q", "-m", plugin, "-m", native, "-m", mutation]
    env = os.environ.copy()
    env["PATH"] = str(runtime / "bin") + os.pathsep + env.get("PATH", "")
    source = official / "test4.v"
    map_v = ROOT / "pmux_project/pass/pmux_case34_lut_map.v"
    abc = ROOT / "pmux_project/pass/pmux_case34_abc.script"
    rows = []

    for consumer in ("native", "runner"):
        for mode in MODES:
            prefix = f"{consumer}_{mode}"
            if consumer == "native":
                script = (
                    f"scratchpad -set pmux_test.profile_mutation {mode}; "
                    f"read_verilog {source}; "
                    "synth_intel -family cycloneiv -top test4 -run begin:map_cells; "
                    "scratchpad -assert pmux_opt.accepted_constant_table false; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\"; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_generation \"\"; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false"
                )
                marker = r"synth_intel: pmux_profile="
            else:
                script = (
                    f"scratchpad -set pmux_test.profile_mutation {mode}; "
                    f"read_verilog {source}; "
                    "synth_intel -family cycloneiv -top test4 -run begin:map_ffram; "
                    "opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; "
                    "pmux_opt; opt_clean; techmap -map +/techmap.v; opt -full; clean -purge; "
                    "setundef -undriven -zero; "
                    f"pmux_opt_abc9 -family cycloneiv -top test4 -cofactor-map {map_v} -abc-script {abc}; "
                    "scratchpad -assert pmux_opt.accepted_constant_table false; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\"; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_generation \"\"; "
                    "scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false"
                )
                marker = r"pmux_opt_abc9: profile="
            completed = run([*base, "-p", script], log=logs / f"{prefix}.log", env=env)
            actual = profile(completed.stdout, marker)
            expected = "accepted_constant_table" if mode == "none" else "stock"
            rejected = "authorization rejected reason=" in completed.stdout
            passed = completed.returncode == 0 and actual == expected and (mode == "none" or rejected)
            rows.append({"consumer": consumer, "mode": mode, "expected": expected,
                         "actual": actual, "rejected": rejected, "returncode": completed.returncode,
                         "pass": passed})
            print("PASS" if passed else "FAIL", consumer, mode, actual)

    for consumer in ("native", "runner"):
        if consumer == "native":
            script = (
                "scratchpad -set pmux_test.profile_mutation none; "
                f"read_verilog {source}; "
                "synth_intel -family cycloneiv -top test4 -run begin:map_ffs; "
                "select test4; pmux_test_add_lut4; select *; "
                "synth_intel -family cycloneiv -top test4 -run map_ffram:map_luts; "
                "scratchpad -assert pmux_opt.accepted_constant_table false"
            )
            marker = r"synth_intel: pmux_profile=(\w+)"
        else:
            script = (
                "scratchpad -set pmux_test.profile_mutation none; "
                f"read_verilog {source}; "
                "synth_intel -family cycloneiv -top test4 -run begin:map_ffram; "
                "opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; "
                "pmux_opt; opt_clean; techmap -map +/techmap.v; opt -full; clean -purge; "
                "setundef -undriven -zero; "
                f"pmux_opt_abc9 -family cycloneiv -top test4 -cofactor-map {map_v} -abc-script {abc}; "
                "select test4; pmux_test_add_lut4; select *; "
                f"pmux_opt_abc9 -family cycloneiv -top test4 -cofactor-map {map_v} -abc-script {abc}; "
                "scratchpad -assert pmux_opt.accepted_constant_table false"
            )
            marker = r"pmux_opt_abc9: profile=(\w+)"
        completed = run([*base, "-p", script], log=logs / f"{consumer}_consumed_replay.log", env=env)
        actual = re.findall(marker, completed.stdout)
        passed = completed.returncode == 0 and actual[:2] == ["accepted_constant_table", "stock"]
        rows.append({"consumer": consumer, "mode": "consumed_replay",
                     "expected": ["accepted_constant_table", "stock"], "actual": actual,
                     "returncode": completed.returncode, "pass": passed})
        print("PASS" if passed else "FAIL", consumer, "consumed_replay", actual)

    summary = {"tests": rows, "passed": sum(x["pass"] for x in rows), "total": len(rows)}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"DUAL_CONSUMER_LIFECYCLE {summary['passed']}/{summary['total']}")
    return 0 if summary["passed"] == summary["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
