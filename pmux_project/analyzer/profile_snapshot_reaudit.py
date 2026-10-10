#!/usr/bin/env python3
"""A-only real-path audit for constant-table lifecycle authorization."""

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
B_SCRIPTS = ROOT / "pmux_project/experiments/case34_v02b/scripts"


def checked(command, *, cwd, env=None, log=None):
    result = subprocess.run(
        [str(item) for item in command], cwd=cwd, env=env, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
    )
    if log is not None:
        log.write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(map(str, command))}\n{result.stdout}")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    owner = None
    if args.output is None:
        owner = tempfile.TemporaryDirectory(prefix="pmux-a-snapshot-reaudit-")
        output = Path(owner.name)
    else:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=True)
    build, runtime, logs = output / "build", output / "runtime", output / "logs"
    for directory in (build, runtime / "bin", runtime / "share", logs):
        directory.mkdir(parents=True, exist_ok=True)

    tool = args.tool_root.resolve()
    yosys_config = tool / "bin/yosys-config"
    shutil.copy2(tool / "bin/yosys", runtime / "bin/yosys")
    shutil.copy2(tool / "bin/yosys-abc", runtime / "bin/yosys-abc")
    shutil.copytree(tool / "share/yosys", runtime / "share/yosys", dirs_exist_ok=True)
    common = runtime / "share/yosys/intel/common"
    shutil.copy2(ROOT / "techlibs/intel/common/pmux_case34_lut_map.v", common)
    shutil.copy2(ROOT / "techlibs/intel/common/pmux_case34_abc.script", common)

    checked(["make", "-C", ROOT / "pmux_project/pass", "clean", "all",
             f"YOSYS_CONFIG={yosys_config}"], cwd=ROOT, log=logs / "build_pmux.log")
    native = build / "native_synth.so"
    audit = build / "profile_snapshot_reaudit.so"
    checked([yosys_config, "--build", native, B_SCRIPTS / "native_synth_loader.cc",
             f"-I{ROOT}"], cwd=ROOT, log=logs / "build_native.log")
    checked([yosys_config, "--build", audit,
             ROOT / "pmux_project/analyzer/profile_snapshot_reaudit_plugin.cc",
             f"-I{ROOT}"], cwd=ROOT, log=logs / "build_audit.log")

    env = dict(os.environ, YOSYS_DATDIR=str(runtime / "share/yosys"))
    base = [runtime / "bin/yosys", "-Q", "-m", ROOT / "pmux_project/pass/pmux_opt.so",
            "-m", native, "-m", audit]
    results = []

    def execute(path, mode, required):
        prefix = (
            f"scratchpad -set pmux_audit.profile_snapshot_mode {mode}; "
            f"read_verilog {args.official.resolve() / 'test4.v'}; "
        )
        if path == "synth_intel":
            body = "synth_intel -family cycloneiv -top test4 -run begin:map_cells; "
            marker = r"synth_intel: pmux_profile=(\w+)"
        else:
            body = (
                "synth_intel -family cycloneiv -top test4 -run begin:map_ffram; "
                "opt -fast -mux_undef -undriven -fine -full; memory_map; "
                "opt -undriven -fine; pmux_opt; opt_clean; "
                "techmap -map +/techmap.v; opt -full; clean -purge; "
                "setundef -undriven -zero; "
                f"pmux_opt_abc9 -family cycloneiv -top test4 "
                f"-cofactor-map {ROOT / 'pmux_project/pass/pmux_case34_lut_map.v'} "
                f"-abc-script {ROOT / 'pmux_project/pass/pmux_case34_abc.script'}; "
            )
            marker = r"pmux_opt_abc9: profile=(\w+)"
        script = prefix + body + (
            "scratchpad -assert pmux_opt.accepted_constant_table false; "
            "scratchpad -assert pmux_opt.accepted_constant_table_snapshot \"\"; "
            "scratchpad -assert pmux_opt.accepted_constant_table_generation \"\"; "
            "scratchpad -assert pmux_opt.accepted_constant_table_lifecycle_valid false"
        )
        run = subprocess.run([*map(str, base), "-p", script], cwd=output, env=env,
                             text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=900)
        (logs / f"{path}_{mode}.log").write_text(run.stdout, encoding="utf-8")
        profiles = re.findall(marker, run.stdout)
        profile = profiles[-1] if profiles else "missing"
        payload_equal = "payload_equal=1" in run.stdout
        lifecycle_match = re.findall(r"lifecycle_valid=(\d+)", run.stdout)
        lifecycle_valid = int(lifecycle_match[-1]) if lifecycle_match else None
        reasons = re.findall(r"authorization rejected reason=(\w+)", run.stdout)
        results.append({"path": path, "mode": mode, "returncode": run.returncode,
                        "required_profile": required, "actual_profile": profile,
                        "payload_equal": payload_equal,
                        "lifecycle_valid_before_consume": lifecycle_valid,
                        "rejection_reasons": reasons,
                        "requirement_met": run.returncode == 0 and profile == required})

    for path in ("synth_intel", "runner"):
        execute(path, "none", "accepted_constant_table")
        execute(path, "same_name_rebuild", "stock")
        execute(path, "port_restore", "stock")
        execute(path, "lifecycle_false", "stock")
        execute(path, "empty_generation", "stock")
        execute(path, "forge_generation", "stock")

    legal = [item for item in results if item["mode"] == "none"]
    rebuilt = [item for item in results if item["mode"] == "same_name_rebuild"]
    restored = [item for item in results if item["mode"] == "port_restore"]
    forged = [item for item in results if item["mode"] == "forge_generation"]
    summary = {"results": results,
               "legal_first_consume_pass": all(item["requirement_met"] for item in legal),
               "previous_rebuild_fixed_both_paths": all(
                   item["returncode"] == 0 and item["payload_equal"] and
                   item["lifecycle_valid_before_consume"] == 0 and
                   item["actual_profile"] == "stock" and
                   "target_lifecycle_mismatch" in item["rejection_reasons"]
                   for item in rebuilt),
               "port_restore_invalidates_both_paths": all(
                   item["returncode"] == 0 and item["payload_equal"] and
                   item["lifecycle_valid_before_consume"] == 0 and
                   item["actual_profile"] == "stock"
                   for item in restored),
               "forged_generation_counterexample_both_paths": all(
                   item["returncode"] == 0 and
                   item["lifecycle_valid_before_consume"] == 1 and
                   item["actual_profile"] == "accepted_constant_table" and
                   not item["requirement_met"] for item in forged)}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if (summary["legal_first_consume_pass"] and
            summary["previous_rebuild_fixed_both_paths"] and
            summary["port_restore_invalidates_both_paths"] and
            summary["forged_generation_counterexample_both_paths"]):
        print("A_PROFILE_LIFECYCLE_FORGED_GENERATION_COUNTEREXAMPLE REPRODUCED")
        return 0
    print("A_PROFILE_LIFECYCLE_AUDIT INCONCLUSIVE")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
