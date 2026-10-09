#!/usr/bin/env python3
"""Build and exercise the real PMUX profile snapshot consumer paths."""

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
SCRIPT_DIR = Path(__file__).resolve().parent
MODES = [
    "none",
    "lut_bit",
    "lut_width",
    "port",
    "attribute",
    "type",
    "remove_one",
    "add_extra",
    "drop_snapshot",
]


def run(command, *, cwd=ROOT, env=None, log=None):
    completed = subprocess.run(
        [str(item) for item in command],
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=900,
    )
    if log is not None:
        log.write_text(completed.stdout, encoding="utf-8")
    return completed


def locate_official_dir(argument):
    candidates = []
    if argument:
        candidates.append(Path(argument))
    candidates.extend([
        ROOT / "pmux_project/pmux_case/competition_case",
        Path("/home/james/work/pmux-yosys/pmux_project/pmux_case/competition_case"),
    ])
    for candidate in candidates:
        if (candidate / "test4.v").is_file():
            return candidate.resolve()
    raise SystemExit("MISSING: official competition_case/test4.v")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--official-dir")
    parser.add_argument("--output", default=str(ROOT / "pmux_project/reports/b-profile-snapshot-tests"))
    parser.add_argument("--tool-root", default="/home/james/pmux/install/yosys-clean")
    args = parser.parse_args()

    output = Path(args.output).resolve()
    build = output / "build"
    runtime = output / "runtime"
    logs = output / "logs"
    for directory in (build, runtime / "bin", runtime / "share", logs):
        directory.mkdir(parents=True, exist_ok=True)

    tool_root = Path(args.tool_root)
    yosys_config = tool_root / "bin/yosys-config"
    shutil.copy2(tool_root / "bin/yosys", runtime / "bin/yosys")
    shutil.copy2(tool_root / "bin/yosys-abc", runtime / "bin/yosys-abc")
    shutil.copytree(tool_root / "share/yosys", runtime / "share/yosys", dirs_exist_ok=True)
    common = runtime / "share/yosys/intel/common"
    shutil.copy2(ROOT / "techlibs/intel/common/pmux_case34_lut_map.v", common)
    shutil.copy2(ROOT / "techlibs/intel/common/pmux_case34_abc.script", common)

    plugin_make = run([
        "make", "-C", ROOT / "pmux_project/pass", "clean", "all",
        f"YOSYS_CONFIG={yosys_config}",
    ], log=logs / "build_pmux_plugin.log")
    if plugin_make.returncode:
        raise SystemExit("FAIL: PMUX plugin build")

    plugins = {
        "native": (SCRIPT_DIR / "native_synth_loader.cc", build / "native_synth.so"),
        "mutation": (SCRIPT_DIR / "profile_snapshot_mutation_plugin.cc", build / "profile_mutation.so"),
    }
    for name, (source, target) in plugins.items():
        compiled = run([
            yosys_config, "--build", target, source, f"-I{ROOT}",
        ], log=logs / f"build_{name}.log")
        if compiled.returncode:
            raise SystemExit(f"FAIL: {name} plugin build")

    official = locate_official_dir(args.official_dir)
    yosys = runtime / "bin/yosys"
    pmux_plugin = ROOT / "pmux_project/pass/pmux_opt.so"
    base = [yosys, "-Q", "-m", pmux_plugin, "-m", plugins["native"][1], "-m", plugins["mutation"][1]]
    results = []
    for mode in MODES:
        log = logs / f"native_{mode}.log"
        script = (
            f"scratchpad -set pmux_test.profile_mutation {mode}; "
            f"read_verilog {official / 'test4.v'}; "
            "synth_intel -family cycloneiv -top test4 -run begin:map_cells; "
            "scratchpad -assert pmux_opt.accepted_constant_table false; "
            "scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\""
        )
        completed = run([*base, "-p", script], log=log)
        matches = re.findall(r"synth_intel: pmux_profile=(\w+)", completed.stdout)
        actual = matches[-1] if matches else "missing"
        expected = "accepted_constant_table" if mode == "none" else "stock"
        rejected = "target_snapshot_mismatch" in completed.stdout
        passed = completed.returncode == 0 and actual == expected and (mode == "none" or rejected)
        results.append({
            "test": mode,
            "returncode": completed.returncode,
            "expected_profile": expected,
            "actual_profile": actual,
            "snapshot_rejected": rejected,
            "pass": passed,
            "log": str(log),
        })

    replay_log = logs / "native_consumed_replay.log"
    replay_script = (
        "scratchpad -set pmux_test.profile_mutation none; "
        f"read_verilog {official / 'test4.v'}; "
        "synth_intel -family cycloneiv -top test4 -run begin:map_ffs; "
        "scratchpad -assert pmux_opt.accepted_constant_table false; "
        "scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\"; "
        "select test4; pmux_test_add_lut4; select *; "
        "synth_intel -family cycloneiv -top test4 -run map_ffram:map_luts; "
        "scratchpad -assert pmux_opt.accepted_constant_table false"
    )
    replay = run([*base, "-p", replay_script], log=replay_log)
    profiles = re.findall(r"synth_intel: pmux_profile=(\w+)", replay.stdout)
    replay_passed = replay.returncode == 0 and profiles[:2] == ["accepted_constant_table", "stock"]
    results.append({
        "test": "consumed_full_cell_replay",
        "returncode": replay.returncode,
        "expected_profiles": ["accepted_constant_table", "stock"],
        "actual_profiles": profiles,
        "pass": replay_passed,
        "log": str(replay_log),
    })

    summary = {
        "tests": results,
        "passed": sum(1 for result in results if result["pass"]),
        "total": len(results),
    }
    (output / "profile_snapshot_results.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(f"PROFILE_SNAPSHOT_TESTS {summary['passed']}/{summary['total']} PASS")
    for result in results:
        print(result["test"], "PASS" if result["pass"] else "FAIL")
    return 0 if summary["passed"] == summary["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
