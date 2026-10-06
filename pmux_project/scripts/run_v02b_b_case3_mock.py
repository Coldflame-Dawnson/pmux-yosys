#!/usr/bin/env python3
"""C's case3 check of B's test certificate path; never a production verdict."""

import argparse
import json
import os
from pathlib import Path

from diagnose_official_stages import hash_file, load_b_entry
from run_case3_variant_matrix import (EXPECTED_BASELINE, archive_evidence,
                                      evaluate, flow_commands)
from run_micro import run, save
from run_official import HASHES, timing

INPUT_SHA = "baa2a34eb6fd25db3950defbc141a1477d7114b38eee971d9b02cca12d84af1e"
EXPECTED_SHA = "3013f07a9202646a85b7dd1f9e8d0f6dd6d132d75c1009e6a5f9aa7895db0266"


def main():
    parser = argparse.ArgumentParser()
    for name in ("bin", "plugin", "before", "expected-json", "official-source", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    for name in ("bin", "plugin", "before", "expected_json", "official_source", "out"):
        setattr(args, name, getattr(args, name).resolve())
    yosys, eqy = args.bin / "yosys", args.bin / "eqy"
    if any(not path.is_file() for path in (yosys, eqy, args.plugin, args.before,
                                           args.expected_json, args.official_source)):
        parser.error("required frozen tool, source, or plugin missing")
    if hash_file(args.before) != INPUT_SHA or hash_file(args.expected_json) != EXPECTED_SHA:
        parser.error("case3 frozen before/expected JSON SHA-256 mismatch")
    if hash_file(args.official_source) != HASHES[2]:
        parser.error("official case3 RTL SHA-256 mismatch")
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("use a fresh output directory")
    args.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(args.bin) + os.pathsep + env.get("PATH", "")
    plan, stats = args.out / "plan.json", args.out / "stats.json"
    after, after_json = args.out / "after.il", args.out / "after.json"
    script = (f'read_rtlil "{args.before}"; '
              'pmux_opt -v02-branch-delete -test-branch-proof $procmux$18 0 '
              f'-write-plan "{plan}" -write-stats "{stats}"; '
              f'check -assert -noinit; write_json "{after_json}"; '
              f'select test3; write_rtlil -selected "{after}"\n')
    save(args.out / "pass.ys", script)
    command = [yosys, "-Q", "-m", args.plugin, "-l", args.out / "pass.log",
               "-s", args.out / "pass.ys"]
    save(args.out / "pass.command.txt", " ".join(map(str, command)) + "\n")
    rc = run(command, args.out / "pass.console.log", args.out, env, timeout=900)
    if rc:
        raise RuntimeError(f"B test-only Pass failed with rc={rc}")
    machine_plan = json.loads(plan.read_text(encoding="utf-8"))
    machine_stats = json.loads(stats.read_text(encoding="utf-8"))
    actual = json.loads(after_json.read_text(encoding="utf-8"))["modules"]["test3"]
    expected = json.loads(args.expected_json.read_text(encoding="utf-8"))["modules"]["test3"]
    edits = machine_plan.get("branch_edits", [])
    machine_ok = bool(
        machine_plan.get("mode") == "testing_v02_branch_mock" and
        machine_stats.get("mode") == "testing_v02_branch_mock" and
        machine_plan.get("commit") == "ok" and len(edits) == 1 and
        edits[0].get("cell") == "$procmux$18" and
        edits[0].get("deleted_branches") == [0] and
        edits[0].get("removed_b_ranges") == [{"branch": 0, "offset": 0, "width": 1}] and
        edits[0].get("removed_s_indices") == [0] and
        edits[0].get("old_s_width") == 2 and edits[0].get("new_s_width") == 1 and
        edits[0].get("transformation") == "pmux_to_mux" and
        machine_stats.get("branch_rewrite", {}).get("committed_branches") == 1 and
        machine_stats.get("changed_bits") == len(machine_plan.get("edits", [])) == 0)
    prefix, suffix = flow_commands(load_b_entry())
    baseline = evaluate("baseline", args.before, args.official_source, prefix, suffix,
                        yosys, eqy, args.out, env, "OFFICIAL_BASELINE")
    mock = evaluate("B_test_certificate", after, args.official_source, prefix, suffix,
                    yosys, eqy, args.out, env, "B_TEST_CERTIFICATE")
    baseline_resources = {key: baseline[key] for key in ("lut", "dff", "io")}
    mock_resources = {key: mock[key] for key in ("lut", "dff", "io")}
    mock_cpu, mock_peak = timing((args.out / "B_test_certificate" / "post_abc9.log").read_text(
        encoding="utf-8", errors="replace"))
    verdict = ("PASS_TEST_ONLY" if machine_ok and actual == expected and
               baseline["check"] == mock["check"] == "PASS" and
               baseline["eqy"] == mock["eqy"] == "PASS" and
               baseline_resources == EXPECTED_BASELINE and
               mock_resources == {"lut": 12, "dff": 17, "io": 0} else "FAIL")
    report = {
        "schema": 1, "mode": "B_TEST_CERTIFICATE_CASE3",
        "a_proof_provider": "NOT_CONNECTED",
        "b_commit": "bff595118a130756f504af99c09000c98adda4ba",
        "plugin_sha256": hash_file(args.plugin),
        "input_sha256": hash_file(args.before),
        "expected_json_sha256": hash_file(args.expected_json),
        "after_rtlil_sha256": hash_file(after),
        "machine_contract": "PASS" if machine_ok else "FAIL",
        "top_structure_exact": actual == expected,
        "baseline": baseline, "test_certificate": mock,
        "resources_baseline": baseline_resources,
        "resources_test_certificate": mock_resources,
        "test_certificate_cpu_s": mock_cpu,
        "test_certificate_peak_mb": mock_peak,
        "result": verdict,
        "production_lut_gain": "NOT_RUN",
        "ready_to_merge_integration_g3": False,
    }
    save(args.out / "summary.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    save(args.out / "summary.md", "# case3 B test-only certificate\n\n"
         f"结果：{verdict}；机器计划：{report['machine_contract']}；目标模块结构精确相同："
         f"{report['top_structure_exact']}。\n\n"
         f"基线 {baseline_resources}，test-only certificate {mock_resources}；"
         f"EQY={mock['eqy']}，方法={mock['eqy_method']}。\n\n"
         "A 真实证明提供者未接入，13→12 LUT 只能归于 test-only 结构路径，"
         "不得计作生产收益。\n")
    archive_evidence(args.out)
    print(verdict, baseline_resources, mock_resources)
    return 0 if verdict == "PASS_TEST_ONLY" else 1


if __name__ == "__main__":
    raise SystemExit(main())
