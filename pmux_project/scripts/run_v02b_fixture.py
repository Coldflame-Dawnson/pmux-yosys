#!/usr/bin/env python3
"""C-only V2-B fixture validation. No result here is a production Pass result."""

import argparse
import hashlib
import json
import os
import random
import shlex
import subprocess
import sys
import tarfile
from pathlib import Path

from diagnose_official_stages import metrics
from generate_v02b_cases import OUT as MICRO, build
from run_micro import ROOT, run, save
from run_official import timing
from run_random import SEEDS, eqy_config


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_top(path):
    return json.loads(path.read_text(encoding="utf-8"))["modules"]["top"]


def run_yosys(yosys, work, env, label, script):
    ys = work / (label + ".ys")
    log = work / (label + ".log")
    save(ys, script + "\n")
    rc = run([yosys, "-l", log, "-s", ys], work / (label + ".console.log"),
             work, env, timeout=300)
    if rc:
        raise RuntimeError(f"{label}: Yosys rc={rc}; see {log}")
    return log


def parsed_width(value):
    return int(value, 2) if isinstance(value, str) else int(value)


def symbolic_cell(module, cell):
    names = {}
    for name, net in sorted(module["netnames"].items()):
        for index, bit in enumerate(net["bits"]):
            if isinstance(bit, int):
                names.setdefault(bit, (name, index))
    return {key: ({"connections": {
        port: [names.get(bit, bit) for bit in bits]
        for port, bits in cell["connections"].items()}
        } if key == "connections" else value)
        for key, value in cell.items()}


def check_structure(before, after, spec, plan, stats):
    issues = []
    target = spec["target"]
    original = before["cells"].get(target)
    transformed = after["cells"].get(target)
    removed = spec["removed_branches"]
    expected_plan = ([{"module": "top", "cell": target,
                       "removed_branches": removed,
                       "result_type": spec["target_after_type"]}] if removed else [])
    if plan.get("mode") != "fixture" or plan.get("edits") != expected_plan:
        issues.append("fixture machine plan differs from expectation")
    if (stats.get("mode") != "fixture" or stats.get("changed_cells") != int(bool(removed))
            or stats.get("reason_codes") != [spec["reason_category"]]):
        issues.append("fixture statistics/reason differs from expectation")
    if not removed:
        if before != after:
            issues.append("expected refusal/rollback changed the top netlist")
        if spec["expect_atomic_zero_write"] and plan.get("edits"):
            issues.append("atomic failure plan is not empty")
        return issues
    if original is None or original["type"] != "$pmux":
        return issues + ["before target is not a PMUX"]
    if len(set(removed)) != len(removed) or any(i < 0 or i >= spec["branches_before"] for i in removed):
        return issues + ["invalid fixture branch selection"]
    if parsed_width(original["parameters"]["S_WIDTH"]) != spec["branches_before"]:
        issues.append("before branch count mismatch")
    keep = [i for i in range(spec["branches_before"]) if i not in removed]
    original_width = parsed_width(original["parameters"]["WIDTH"])
    old_b = original["connections"]["B"]
    old_s = original["connections"]["S"]
    expected_b = [bit for i in keep for bit in old_b[i * original_width:(i + 1) * original_width]]
    expected_s = [old_s[i] for i in keep]
    if set(after["cells"]) - set(before["cells"]):
        issues.append("new cell appeared")
    for name, cell in before["cells"].items():
        if name != target and (
            name not in after["cells"] or
            symbolic_cell(before, cell) != symbolic_cell(after, after["cells"][name])
        ):
            issues.append(f"non-target cell changed: {name}")
    if spec["target_after_type"] == "DELETED":
        if transformed is not None:
            issues.append("last branch did not delete target cell")
        if after["ports"]["out"]["bits"] != after["ports"]["a"]["bits"]:
            issues.append("deleted target did not reconnect output to A")
        return issues
    if transformed is None or transformed["type"] != spec["target_after_type"]:
        return issues + ["after target type mismatch"]
    connections = transformed["connections"]
    for port in ("A", "Y"):
        if connections.get(port) != original["connections"][port]:
            issues.append(f"{port} connection changed")
    if connections.get("B") != expected_b or connections.get("S") != expected_s:
        issues.append("physical B/S branch slices differ from planned removal")
    if parsed_width(transformed["parameters"]["WIDTH"]) != original_width:
        issues.append("data width changed")
    if transformed["type"] == "$pmux":
        if parsed_width(transformed["parameters"]["S_WIDTH"]) != len(keep):
            issues.append("remaining PMUX branch count mismatch")
    elif "S_WIDTH" in transformed["parameters"]:
        issues.append("converted MUX retains S_WIDTH parameter")
    return issues


def fixture_machine(spec):
    removed = spec["removed_branches"]
    edits = ([{"module": "top", "cell": spec["target"],
               "removed_branches": removed,
               "result_type": spec["target_after_type"]}] if removed else [])
    plan = {"schema": 1, "mode": "fixture", "edits": edits,
            "origin": "prebuilt_expected_after"}
    stats = {"schema": 1, "mode": "fixture",
             "changed_cells": int(bool(removed)),
             "reason_codes": [spec["reason_category"]],
             "budget_origin": "fixture_forced" if spec["budget"] else None}
    return plan, stats


def one(spec, before_text, after_text, out, yosys, eqy, env, versions, reproduce):
    work = out / spec["id"]
    work.mkdir(parents=True, exist_ok=True)
    before, after = work / "before.il", work / "after.il"
    save(before, before_text)
    save(after, after_text)
    plan, stats = fixture_machine(spec)
    save(work / "plan.json", json.dumps(plan, indent=2) + "\n")
    save(work / "stats.json", json.dumps(stats, indent=2) + "\n")
    save(work / "expectation.json", json.dumps(spec, indent=2) + "\n")
    row = {"id": spec["id"], "mode": "FIXTURE", "reason_category": spec["reason_category"],
           "seed": spec.get("seed"), "select_relation": spec["select_relation"],
           "budget_origin": stats["budget_origin"], "structure": "NOT_RUN",
           "check": "NOT_RUN", "eqy": "NOT_RUN", "resources": "NOT_RUN",
           "pre_abc9_comb": None,
           "lut_before": None, "lut_after": None, "dff_before": None,
           "dff_after": None, "io_before": None, "io_after": None,
           "cpu_before_s": None, "cpu_after_s": None,
           "peak_before_mb": None, "peak_after_mb": None, "issues": [],
           "input_sha256": digest(before), "after_sha256": digest(after)}
    try:
        for label, path in (("before", before), ("after", after)):
            script = (f'read_rtlil "{path}"; hierarchy -check -top top; '
                      f'check -assert -noinit; write_json "{work / (label + ".json")}"')
            run_yosys(yosys, work, env, label + "_check", script)
        row["check"] = "PASS"
        old, new = load_top(work / "before.json"), load_top(work / "after.json")
        issues = check_structure(old, new, spec, plan, stats)
        row["issues"].extend(issues)
        row["structure"] = "PASS" if not issues else "FAIL"
        for label, path in (("before", before), ("after", after)):
            for stage, stop in (("pre", "map_luts"), ("post", "map_cells")):
                script = (f'read_rtlil "{path}"; '
                          f'synth_intel -family cycloneiv -top top -run begin:{stop}; '
                          f'write_json "{work / (label + "_" + stage + ".json")}"')
                log = run_yosys(yosys, work, env, label + "_" + stage, script)
                if stage == "post":
                    cpu, peak = timing(log.read_text(encoding="utf-8", errors="replace"))
                    row["cpu_" + label + "_s"] = cpu
                    row["peak_" + label + "_mb"] = peak
        pre = metrics(load_top(work / "after_pre.json"))
        old_metric = metrics(load_top(work / "before_post.json"))
        new_metric = metrics(load_top(work / "after_post.json"))
        row["pre_abc9_comb"] = pre["total_comb"]
        row["pre_abc9_gates"] = {k: pre[k] for k in ("and", "or", "not", "basic_mux")}
        for key in ("lut", "dff", "io"):
            row[key + "_before"], row[key + "_after"] = old_metric[key], new_metric[key]
        row["resources"] = ("PASS" if row["dff_after"] <= row["dff_before"]
                            and row["io_after"] <= row["io_before"] else "FAIL")
        if row["resources"] == "FAIL":
            row["issues"].append("DFF or IO resource regression")
        cfg = work / "verify.eqy"
        save(cfg, eqy_config(before, after, old))
        rc = run([eqy, "-f", "-d", work / "eqy_work", cfg],
                 work / "eqy.log", work, env, timeout=300)
        row["eqy"] = ("PASS" if rc == 0 and "DONE (PASS" in
                      (work / "eqy.log").read_text(encoding="utf-8", errors="replace") else "FAIL")
    except Exception as exc:
        row["issues"].append(str(exc))
    row["result"] = "PASS" if all(row[k] == "PASS" for k in
                                  ("structure", "check", "eqy", "resources")) else "FAIL"
    if row["result"] == "FAIL":
        save(work / "failure.json", json.dumps({
            "id": spec["id"], "mode": "FIXTURE", "issues": row["issues"],
            "versions": versions, "reproduce": reproduce,
            "before_sha256": row["input_sha256"],
            "after_sha256": row["after_sha256"]}, indent=2) + "\n")
    return row


def random_case(seed):
    rng = random.Random(seed)
    exclusive = seed % 2 == 0
    count = rng.choice((2, 3, 4))
    width = rng.choice((1, 2, 3))
    removed = (rng.randrange(count),) if exclusive else ()
    equal = removed if exclusive else ()
    name = f"seed_{seed:08d}"
    before, after, spec = build(name, count, removed, width,
                                selector="exclusive" if exclusive else "overlap",
                                equal_a=equal,
                                reason="fixture_branch_delete" if exclusive else
                                "fixture_select_overlap")
    spec["seed"] = seed
    return before, after, spec


def archive_evidence(out):
    paths = sorted(path for path in out.rglob("*") if path.is_file()
                   and path.name not in ("evidence.tar.gz", "SHA256SUMS.txt"))
    save(out / "SHA256SUMS.txt",
         "\n".join(f"{digest(path)}  {path.relative_to(out).as_posix()}"
                   for path in paths) + "\n")
    with tarfile.open(out / "evidence.tar.gz", "w:gz") as archive:
        for path in paths + [out / "SHA256SUMS.txt"]:
            archive.add(path, arcname=path.relative_to(out).as_posix())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("micro", "random"), required=True)
    parser.add_argument("--count", type=int, choices=(100, 1000), default=100)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.bin = args.bin.resolve()
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("use a fresh output directory")
    yosys, eqy = args.bin / "yosys", args.bin / "eqy"
    if not yosys.is_file() or not eqy.is_file():
        parser.error("missing frozen Yosys/EQY")
    args.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["PATH"] = str(args.bin) + os.pathsep + env.get("PATH", "")
    versions = {"yosys": subprocess.run([yosys, "-V"], capture_output=True,
                    text=True).stdout.strip(),
                "eqy": subprocess.run([eqy, "--version"], capture_output=True,
                    text=True).stdout.strip()}
    if args.suite == "micro":
        manifest = json.loads((MICRO / "expectations.json").read_text(encoding="utf-8"))
        inputs = [(row, (MICRO / row["before"]).read_text(encoding="utf-8"),
                   (MICRO / row["after"]).read_text(encoding="utf-8"))
                  for row in manifest["cases"]]
    else:
        all_seeds = [int(line) for line in SEEDS.read_text().splitlines() if line.strip()]
        if len(all_seeds) != 1000 or len(set(all_seeds)) != 1000:
            parser.error("frozen random seed inventory invalid")
        selected = [args.seed] if args.seed is not None else all_seeds[:args.count]
        inputs = []
        for seed in selected:
            before, after, spec = random_case(seed)
            inputs.append((spec, before, after))
    rows = []
    for spec, before, after in inputs:
        reproduce = (f"python3 pmux_project/scripts/run_v02b_fixture.py "
                     f"--suite {args.suite} --bin {shlex.quote(str(args.bin))} "
                     f"--out <fresh-dir>" +
                     (f" --seed {spec['seed']}" if "seed" in spec else ""))
        row = one(spec, before, after, args.out, yosys, eqy, env, versions, reproduce)
        rows.append(row)
        print(f"{row['id']}: {row['result']} structure={row['structure']} "
              f"EQY={row['eqy']} LUT={row['lut_before']}->{row['lut_after']}", flush=True)
    report = {"schema": 1, "mode": "FIXTURE", "suite": args.suite,
              "versions": versions, "count": len(rows),
              "pass": sum(row["result"] == "PASS" for row in rows),
              "fail": sum(row["result"] == "FAIL" for row in rows),
              "real_pass": "NOT_RUN", "real_proof_engine": "NOT_RUN",
              "production_lut_gain": "NOT_RUN", "rows": rows}
    save(args.out / "summary.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    lines = [f"# V2-B {args.suite} fixture regression", "",
             f"Fixture PASS {report['pass']}/{report['count']}; FAIL {report['fail']}. "
             "真实 Pass、生产 LUT 收益、1000 例真实阶段验收均为 NOT_RUN。", "",
             "| 用例 | 原因类别 | 结构 | Check | EQY | ABC9前组合cell | LUT前→后 | DFF前→后 | IO前→后 | CPU s | 峰值MB |",
             "|---|---|---|---|---|---:|---:|---:|---:|---:|---:|"]
    for row in rows:
        lines.append(f"| {row['id']} | {row['reason_category']} | {row['structure']} | "
                     f"{row['check']} | {row['eqy']} | {row['pre_abc9_comb']} | "
                     f"{row['lut_before']}→{row['lut_after']} | "
                     f"{row['dff_before']}→{row['dff_after']} | "
                     f"{row['io_before']}→{row['io_after']} | "
                     f"{row['cpu_after_s']} | {row['peak_after_mb']} |")
    lines += ["", "所有改写和拒绝均为预制 fixture；预算耗尽为强制注入。"
              "本表不判定生产 LUT 收益。", ""]
    save(args.out / "summary.md", "\n".join(lines))
    archive_evidence(args.out)
    return 1 if report["fail"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
