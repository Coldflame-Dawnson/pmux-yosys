#!/usr/bin/env python3
"""Independently validate B's v0.2 machine contract with test certificates.

This never asserts that A proved a branch redundant.  The Pass receives
test-only certificates only for the pre-approved safe structural fixtures.
"""

import argparse
import json
import os
import subprocess
from pathlib import Path

from diagnose_official_stages import metrics
from run_micro import run, save
from run_official import timing
from run_random import eqy_config
from run_v02b_fixture import (MICRO, archive_evidence, check_structure,
                              digest, fixture_machine, load_top, parsed_width,
                              random_case, run_yosys)
from run_random import SEEDS


SPECIAL = {
    "12_partial_bits_reject": {"kind": "partial", "branch": 0,
                               "reason": "branch_partial_proof"},
    "16_keep": {"kind": "proof", "branches": [0],
                "reason": "keep_attribute"},
    "18_snapshot_mismatch": {"kind": "snapshot", "branches": [0],
                             "reason": "test_branch_snapshot_mismatch"},
    "19_invalid_branch": {"kind": "invalid", "branches": [2],
                          "reason": "invalid_branch_rewrite_metadata"},
}


def behavior(spec):
    if spec["id"] in SPECIAL:
        return SPECIAL[spec["id"]]
    if spec["removed_branches"]:
        return {"kind": "proof", "branches": spec["removed_branches"],
                "reason": None}
    return {"kind": "no_provider", "branches": [], "reason": None}


def pass_command(spec, plan, stats):
    behavior_spec = behavior(spec)
    args = ["-v02-branch-delete"]
    if behavior_spec["kind"] == "partial":
        args += ["-test-branch-partial", spec["target"],
                 str(behavior_spec["branch"])]
    else:
        for branch in behavior_spec["branches"]:
            args += ["-test-branch-proof", spec["target"], str(branch)]
    if behavior_spec["kind"] == "snapshot":
        args += ["-test-branch-snapshot-mismatch"]
    args += ["-write-plan", str(plan), "-write-stats", str(stats)]
    if spec["id"] == "17_selection_partial":
        args += ["c:target"]
    return "pmux_opt " + " ".join(args)


def verify_machine(spec, old, new, plan, stats):
    issues = []
    behavior_spec = behavior(spec)
    mocked = behavior_spec["kind"] != "no_provider"
    expected_mode = ("testing_v02_branch_mock" if mocked else
                     "v02_branch_delete_no_provider")
    for document, name in ((plan, "plan"), (stats, "stats")):
        if (document.get("schema") != 1 or document.get("producer") != "pmux_opt"
                or document.get("mode") != expected_mode):
            issues.append(f"{name}: schema/producer/mode mismatch")
    if not isinstance(plan.get("edits"), list) or not isinstance(plan.get("branch_edits"), list):
        return issues + ["plan edits/branch_edits missing"]
    if stats.get("changed_bits") != len(plan["edits"]):
        issues.append("changed_bits differs from physical edits")
    if plan["edits"]:
        issues.append("unexpected physical bit edits in branch-only fixture")
    branch_stats = stats.get("branch_rewrite", {})
    removed = spec["removed_branches"]
    aborted = behavior_spec["kind"] in ("snapshot", "invalid")
    expected_commit = "aborted" if aborted else "ok"
    if plan.get("commit") != expected_commit:
        issues.append("commit state differs from expectation")
    if stats.get("rewrite", {}).get("commit") != expected_commit:
        issues.append("stats commit state differs from plan")
    if aborted and plan.get("commit_reason") != behavior_spec["reason"]:
        issues.append("atomic abort reason differs from contract")
    if branch_stats.get("committed_branches") != len(removed):
        issues.append("committed_branches differs from expected deletion")
    if branch_stats.get("planned_branches") != (0 if aborted else len(removed)):
        # An aborted transaction may have planned candidates before precheck.
        if not aborted:
            issues.append("planned_branches differs from expected deletion")
    reason = behavior_spec["reason"]
    if reason and reason not in stats.get("reason_codes", []) and reason != plan.get("commit_reason"):
        issues.append(f"missing machine reason {reason}")
    if behavior_spec["kind"] == "partial" and branch_stats.get("rejected_partial") != 1:
        issues.append("partial certificate was not rejected")
    if not removed:
        if plan["branch_edits"]:
            issues.append("rejected/no-provider case emitted branch edit")
        if old != new:
            issues.append("rejected/no-provider case changed netlist")
        return issues
    if len(plan["branch_edits"]) != 1:
        return issues + ["expected one merged branch edit"]
    edit = plan["branch_edits"][0]
    original = old["cells"][spec["target"]]
    width = parsed_width(original["parameters"]["WIDTH"])
    expected_ranges = [{"branch": i, "offset": i * width, "width": width}
                       for i in removed]
    expected_fields = {
        "module": "top", "cell": spec["target"],
        "deleted_branches": removed, "removed_b_ranges": expected_ranges,
        "removed_s_indices": removed, "width": width,
        "old_s_width": spec["branches_before"],
        "new_s_width": spec["branches_after"],
        "old_type": "$pmux",
        "new_type": (spec["target_after_type"] if spec["target_after_type"] != "DELETED"
                     else "connection"),
        "transformation": ("shrink_pmux" if spec["branches_after"] >= 2 else
                           "pmux_to_mux" if spec["branches_after"] == 1 else
                           "bypass_a"),
    }
    for key, value in expected_fields.items():
        if edit.get(key) != value:
            issues.append(f"branch edit {key} differs from physical change")
    if len(edit.get("proof_ids", [])) != len(removed):
        issues.append("branch proof ID cardinality mismatch")
    return issues


def one(spec, before_text, root, yosys, eqy, plugin, env, suite):
    work = root / spec["id"]
    work.mkdir(parents=True, exist_ok=True)
    local_before = work / "before.il"
    save(local_before, before_text)
    plan_path, stats_path = work / "plan.json", work / "stats.json"
    after = work / "after.il"
    command = pass_command(spec, plan_path, stats_path)
    save(work / "pass.ys", (f'read_rtlil "{local_before}"; {command}; '
                            f'check -assert -noinit; write_rtlil "{after}"; '
                            f'write_json "{work / "after.json"}"\n'))
    save(work / "pass.command.txt", " ".join((str(yosys), "-Q", "-m", str(plugin),
                                                "-l", str(work / "pass.log"),
                                                "-s", str(work / "pass.ys"))) + "\n")
    row = {"id": spec["id"], "seed": spec.get("seed"),
           "mode": "TEST_CERTIFICATE" if behavior(spec)["kind"] != "no_provider"
           else "NO_PROVIDER", "structure": "NOT_RUN", "machine_contract": "NOT_RUN",
           "check": "NOT_RUN", "eqy": "NOT_RUN", "resources": "NOT_RUN",
           "proof": "NOT_VERIFIED", "lut_before": None, "lut_after": None,
           "dff_before": None, "dff_after": None, "io_before": None,
           "io_after": None, "pre_abc9_comb": None,
           "cpu_after_s": None, "peak_after_mb": None,
           "before_sha256": digest(local_before), "issues": []}
    try:
        run_yosys(yosys, work, env, "before_parse",
                  f'read_rtlil "{local_before}"; check -assert -noinit; '
                  f'write_json "{work / "before.json"}"')
        rc = run([yosys, "-Q", "-m", plugin, "-l", work / "pass.log", "-s", work / "pass.ys"],
                 work / "pass.console.log", work, env, timeout=300)
        if rc:
            raise RuntimeError(f"Pass exited {rc}")
        row["check"] = "PASS"
        old, new = load_top(work / "before.json"), load_top(work / "after.json")
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        stats = json.loads(stats_path.read_text(encoding="utf-8"))
        issues = verify_machine(spec, old, new, plan, stats)
        row["machine_contract"] = "PASS" if not issues else "FAIL"
        row["issues"].extend(issues)
        fixture_plan, fixture_stats = fixture_machine(spec)
        issues = check_structure(old, new, spec, fixture_plan, fixture_stats)
        row["structure"] = "PASS" if not issues else "FAIL"
        row["issues"].extend(issues)
        for label, path in (("before", local_before), ("after", after)):
            for stage, stop in (("pre", "map_luts"), ("post", "map_cells")):
                log = run_yosys(yosys, work, env, label + "_" + stage,
                                f'read_rtlil "{path}"; '
                                f'synth_intel -family cycloneiv -top top -run begin:{stop}; '
                                f'write_json "{work / (label + "_" + stage + ".json")}"')
                if label == "after" and stage == "post":
                    row["cpu_after_s"], row["peak_after_mb"] = timing(
                        log.read_text(encoding="utf-8", errors="replace"))
        pre = metrics(load_top(work / "after_pre.json"))
        old_resource = metrics(load_top(work / "before_post.json"))
        new_resource = metrics(load_top(work / "after_post.json"))
        row["pre_abc9_comb"] = pre["total_comb"]
        row["pre_abc9_gates"] = {key: pre[key] for key in ("and", "or", "not", "basic_mux")}
        for key in ("lut", "dff", "io"):
            row[key + "_before"] = old_resource[key]
            row[key + "_after"] = new_resource[key]
        row["resources"] = "PASS" if (row["dff_after"] <= row["dff_before"] and
                                          row["io_after"] <= row["io_before"]) else "FAIL"
        if row["resources"] == "FAIL":
            row["issues"].append("DFF/IO increased")
        cfg = work / "verify.eqy"
        save(cfg, eqy_config(local_before, after, old))
        rc = run([eqy, "-f", "-d", work / "eqy_work", cfg],
                 work / "eqy.log", work, env, timeout=300)
        row["eqy"] = "PASS" if rc == 0 and "DONE (PASS" in (work / "eqy.log").read_text(
            encoding="utf-8", errors="replace") else "FAIL"
    except Exception as exc:
        row["issues"].append(str(exc))
    row["result"] = "PASS" if all(row[key] == "PASS" for key in
                                    ("machine_contract", "structure", "check", "eqy", "resources")) else "FAIL"
    if row["result"] == "FAIL":
        save(work / "failure.json", json.dumps({"case": spec["id"], "issues": row["issues"],
            "mode": row["mode"], "before_sha256": row["before_sha256"],
            "reproduce": ("python3 pmux_project/scripts/run_v02b_b_contract.py --bin <frozen-bin> "
                          "--plugin <B-plugin> --out <fresh-dir> " +
                          ("--suite random --seed " + str(spec["seed"]) if suite == "random" else
                           "--suite micro --case " + spec["id"]))}, indent=2) + "\n")
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--plugin", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--suite", choices=("micro", "random"), default="micro")
    parser.add_argument("--count", type=int, choices=(100, 1000), default=100)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--case")
    args = parser.parse_args()
    args.bin, args.plugin, args.out = args.bin.resolve(), args.plugin.resolve(), args.out.resolve()
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("use a fresh output directory")
    yosys, eqy = args.bin / "yosys", args.bin / "eqy"
    if not yosys.is_file() or not eqy.is_file() or not args.plugin.is_file():
        parser.error("frozen binaries or B plugin missing")
    args.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(args.bin) + os.pathsep + env.get("PATH", "")
    if args.suite == "micro":
        if args.seed is not None:
            parser.error("--seed requires --suite random")
        specs = json.loads((MICRO / "expectations.json").read_text(encoding="utf-8"))["cases"]
        if args.case:
            specs = [spec for spec in specs if spec["id"] == args.case]
            if not specs:
                parser.error("case ID not in frozen C matrix")
        inputs = [(spec, (MICRO / spec["before"]).read_text(encoding="utf-8"))
                  for spec in specs]
    else:
        if args.case:
            parser.error("--case requires --suite micro")
        seeds = [int(value) for value in SEEDS.read_text().splitlines() if value.strip()]
        if len(seeds) != 1000 or len(set(seeds)) != 1000:
            parser.error("frozen seed inventory invalid")
        selected = [args.seed] if args.seed is not None else seeds[:args.count]
        inputs = [(spec, before) for seed in selected
                  for before, _after, spec in [random_case(seed)]]
    rows = []
    for spec, before_text in inputs:
        row = one(spec, before_text, args.out, yosys, eqy, args.plugin, env, args.suite)
        rows.append(row)
        print(f"{row['id']}: {row['result']} machine={row['machine_contract']} "
              f"structure={row['structure']} EQY={row['eqy']}", flush=True)
    report = {"schema": 1, "mode": "B_TEST_CERTIFICATE_CONTRACT",
              "suite": args.suite,
              "a_proof_provider": "NOT_CONNECTED", "b_commit":
              "bff595118a130756f504af99c09000c98adda4ba",
              "plugin_sha256": digest(args.plugin),
              "yosys": subprocess.run([yosys, "-V"], capture_output=True,
                                       text=True).stdout.strip(),
              "count": len(rows), "pass": sum(r["result"] == "PASS" for r in rows),
              "fail": sum(r["result"] == "FAIL" for r in rows),
              "real_production_acceptance": "NOT_RUN", "rows": rows}
    save(args.out / "summary.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    save(args.out / "summary.md", "# B test-only certificate machine contract\n\n"
         f"{report['pass']}/{report['count']} PASS，{report['fail']} FAIL。"
         "A 真实证明提供者未接入；本报告不计作真实 Pass、1000 例或生产 LUT 收益。\n\n"
         + "\n".join(f"- {row['id']}: {row['result']}，machine={row['machine_contract']}，"
                     f"structure={row['structure']}，EQY={row['eqy']}，"
                     f"LUT={row['lut_before']}→{row['lut_after']}"
                     for row in rows) + "\n")
    archive_evidence(args.out)
    return 1 if report["fail"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
