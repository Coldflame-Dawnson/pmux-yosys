#!/usr/bin/env python3
"""Report C's V2-B fixture evidence and fail-closed real production stage gate."""

import argparse
import hashlib
import json
import tarfile
from pathlib import Path

from run_micro import ROOT, save

BASE_COMMIT = "b4f4a370717ee4442b9f8832e9fcc78bb22efe0e"
CASE3_B_COMMIT = "fbb9eeb40723ad076e4959d53707827082aa40bd"


def read(path):
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive_official(directory, output):
    files = sorted(path for path in directory.rglob("*") if path.is_file())
    manifest = output / "official_baseline_SHA256SUMS.txt"
    save(manifest, "\n".join(f"{sha(path)}  {path.relative_to(directory).as_posix()}"
                              for path in files) + "\n")
    archive_path = output / "official_baseline_evidence.tar.gz"
    with tarfile.open(archive_path, "w:gz") as archive:
        for path in files:
            archive.add(path, arcname=path.relative_to(directory).as_posix())
        archive.add(manifest, arcname="SHA256SUMS.txt")
    return {"path": str(archive_path), "sha256": sha(archive_path),
            "file_count": len(files)}


def fixture_status(report, count):
    return ("PASS" if report and report.get("mode") == "FIXTURE" and
            report.get("count") == count and report.get("fail") == 0 and
            report.get("pass") == count else "NOT_RUN" if report is None else "FAIL")


def real_status(report, count):
    return ("PASS" if report and report.get("mode") == "REAL_PASS" and
            report.get("count") == count and report.get("fail") == 0 and
            report.get("pass") == count else "NOT_RUN" if report is None else "FAIL")


def official_rows(baseline, optimized):
    if baseline is None:
        raise RuntimeError("official baseline report missing")
    rows = []
    optimized_rows = {row["case"]: row for row in optimized.get("cases", [])} if optimized else {}
    for old in baseline["cases"]:
        name = old["case"]
        new = optimized_rows.get(name)
        rows.append({
            "case": name, "baseline_eqy": old["eqy"],
            "baseline_lut": old["lut"], "baseline_dff": old["dff"],
            "baseline_io": old["io"], "baseline_cpu_s": old["cpu_s"],
            "baseline_peak_mb": old["peak_memory_mb"],
            "known_abc_error": old["abc_internal_error"],
            "optimized_eqy": new.get("eqy", "NOT_RUN") if new else "NOT_RUN",
            "optimized_lut": new.get("lut") if new else None,
            "optimized_dff": new.get("dff") if new else None,
            "optimized_io": new.get("io") if new else None,
            "optimized_cpu_s": new.get("cpu_s") if new else None,
            "optimized_peak_mb": new.get("peak_memory_mb") if new else None,
            "pass_count": new.get("pass_count") if new else None,
            "pass_order": new.get("pass_order", "NOT_RUN") if new else "NOT_RUN",
            "machine_fields_complete": new.get("machine_fields_complete", "NOT_RUN") if new else "NOT_RUN",
            "structure": new.get("structure", "NOT_RUN") if new else "NOT_RUN",
        })
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--micro", type=Path, required=True)
    parser.add_argument("--random100", type=Path, required=True)
    parser.add_argument("--case3-fixture", type=Path, required=True)
    parser.add_argument("--official-baseline", type=Path, required=True)
    parser.add_argument("--a-proof-audit", type=Path)
    parser.add_argument("--real-micro", type=Path)
    parser.add_argument("--real-random100", type=Path)
    parser.add_argument("--real-random1000", type=Path)
    parser.add_argument("--real-official", type=Path)
    parser.add_argument("--a-commit")
    parser.add_argument("--b-commit")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    micro, rand100 = read(args.micro), read(args.random100)
    case3, baseline = read(args.case3_fixture), read(args.official_baseline)
    a_proof = read(args.a_proof_audit) if args.a_proof_audit else None
    if any(item is None for item in (micro, rand100, case3, baseline)):
        parser.error("fixture, case3 experiment, or official baseline report missing")
    if case3.get("mode") != "REAL_VARIANTS":
        raise RuntimeError("case3 input is not the frozen structural experiment")
    b = next(row for row in case3["rows"] if row["variant"] == "V2-B")
    if (b["eqy"] != "PASS" or b["check"] != "PASS" or
            b["lut"] != 12 or case3["rows"][0]["lut"] != 13):
        raise RuntimeError("frozen case3 V2-B experiment is not 13 to 12 LUT")
    real_micro = read(args.real_micro) if args.real_micro else None
    real_100 = read(args.real_random100) if args.real_random100 else None
    real_1000 = read(args.real_random1000) if args.real_random1000 else None
    real_official = read(args.real_official) if args.real_official else None
    official = official_rows(baseline, real_official)
    official_evidence = archive_official(args.official_baseline.parent, args.out)
    directed = (real_status(real_micro, 23) == "PASS" and
                real_micro.get("structure_all") == "PASS" and
                real_micro.get("eqy_all") == "PASS")
    random100 = real_status(real_100, 100) == "PASS"
    random1000 = real_status(real_1000, 1000) == "PASS"
    zero_write = bool(real_micro and real_micro.get("snapshot_mismatch_zero_write") == "PASS")
    case3_real = any(row["case"] == "case3" and row["optimized_lut"] == 12
                     and row["optimized_eqy"] == "PASS" and
                     row["optimized_dff"] is not None and
                     row["optimized_dff"] <= row["baseline_dff"] and
                     row["optimized_io"] is not None and
                     row["optimized_io"] <= row["baseline_io"]
                     for row in official)
    other_official = all(
        row["optimized_eqy"] == "PASS" and row["structure"] == "PASS" and
        row["optimized_dff"] is not None and row["optimized_dff"] <= row["baseline_dff"] and
        row["optimized_io"] is not None and row["optimized_io"] <= row["baseline_io"] and
        row["optimized_lut"] is not None and row["optimized_lut"] <= row["baseline_lut"]
        for row in official if row["case"] != "case3")
    machine = bool(real_micro and real_micro.get("machine_fields_complete") == "PASS" and
                   all(row["machine_fields_complete"] == "PASS" for row in official))
    real_path = bool(args.a_commit and args.b_commit and real_micro and
                     real_100 and real_1000 and real_official and
                     all(row["pass_count"] == 1 and row["pass_order"] == "PASS"
                         for row in official))
    gates = {
        "directed_structure_and_eqy": directed,
        "random_100_zero_failures": random100,
        "random_1000_zero_failures": random1000,
        "snapshot_mismatch_zero_write": zero_write,
        "case3_one_lut_gain": case3_real,
        "other_official_no_regression": other_official,
        "machine_plan_stats_reason_complete": machine,
        "real_a_plus_b_path_and_order": real_path,
    }
    fixture_failures = [{"suite": suite, "id": row["id"],
                         "seed": row.get("seed"), "issues": row["issues"]}
                        for suite, data in (("micro", micro), ("random100", rand100))
                        for row in data["rows"] if row["result"] == "FAIL"]
    report = {
        "schema": 1, "base_commit": BASE_COMMIT,
        "a_production_commit": args.a_commit,
        "b_production_commit": args.b_commit,
        "case3_b_test_only_commit": CASE3_B_COMMIT,
        "a_proof_only": {
            "status": "PASS" if a_proof and a_proof.get("mode") == "A_PROOF_ONLY" and
            a_proof.get("ordinary_selftest", {}).get("status") == "PASS" and
            a_proof.get("slow_selftest", {}).get("status") == "PASS" and
            a_proof.get("case3_proof", {}).get("status") == "PASS" else
            "NOT_RUN" if a_proof is None else "FAIL",
            "commit": a_proof.get("a_commit") if a_proof else None,
            "production_pass": "NOT_RUN",
        },
        "fixture": {
            "micro": fixture_status(micro, 22),
            "random100": fixture_status(rand100, 100),
            "case3_experiment": "PASS",
            "case3_lut": [13, 12],
            "micro_count": micro["count"],
            "random100_count": rand100["count"],
            "failure_cases": fixture_failures,
        },
        "real_pass": {
            "micro": real_status(real_micro, 23),
            "random100": real_status(real_100, 100),
            "random1000": real_status(real_1000, 1000),
            "official": "NOT_RUN" if real_official is None else "RUN",
        },
        "official": official, "gates": gates,
        "evidence": {
            "micro": {"path": str(args.micro.parent / "evidence.tar.gz"),
                      "sha256": sha(args.micro.parent / "evidence.tar.gz")},
            "random100": {"path": str(args.random100.parent / "evidence.tar.gz"),
                          "sha256": sha(args.random100.parent / "evidence.tar.gz")},
            "case3_fixture": {"path": str(args.case3_fixture.parent / "evidence.tar.gz"),
                              "sha256": sha(args.case3_fixture.parent / "evidence.tar.gz")},
            "official_baseline": official_evidence,
        },
        "ready_to_merge_integration_g3": all(gates.values()),
    }
    save(args.out / "summary.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    lines = ["# PMUX v0.2-B C 验证门槛", "",
             f"ready_to_merge_integration_g3={str(report['ready_to_merge_integration_g3']).lower()}。",
             "A/B 真实生产提交尚未交付；fixture 及冻结 case3 消融不作为生产规则收益。", "",
             f"A 独立证明自测：{report['a_proof_only']['status']}；生产 Pass：NOT_RUN。", "",
             f"定向 fixture：{report['fixture']['micro']} ({micro['pass']}/{micro['count']})；"
             f"随机 100 fixture：{report['fixture']['random100']} ({rand100['pass']}/{rand100['count']})；"
             "随机 1000 真实验收：NOT_RUN。", "",
             "| case | baseline LUT | optimized LUT | baseline DFF/IO | optimized DFF/IO | baseline CPU s/峰值MB | optimized CPU s/峰值MB | EQY | Pass 审计 | 已知 ABC 报错 |",
             "|---|---:|---:|---|---|---|---|---|---|---|"]
    for row in official:
        optimized_resources = (
            f"{row['optimized_dff']}/{row['optimized_io']}"
            if row["optimized_dff"] is not None and row["optimized_io"] is not None
            else "NOT_RUN")
        optimized_cost = (
            f"{row['optimized_cpu_s']}/{row['optimized_peak_mb']}"
            if row["optimized_cpu_s"] is not None and row["optimized_peak_mb"] is not None
            else "NOT_RUN")
        lines.append(f"| {row['case']} | {row['baseline_lut']} | "
                     f"{row['optimized_lut'] if row['optimized_lut'] is not None else 'NOT_RUN'} | "
                     f"{row['baseline_dff']}/{row['baseline_io']} | "
                     f"{optimized_resources} | "
                     f"{row['baseline_cpu_s']}/{row['baseline_peak_mb']} | "
                     f"{optimized_cost} | "
                     f"{row['baseline_eqy']}→{row['optimized_eqy']} | {row['pass_order']} | "
                     f"{row['known_abc_error']} |")
    lines += ["", "冻结 case3 结构消融 V2-B 再次通过 EQY，13→12 LUT；"
              "这不计入上表 optimized 生产列。", "",
              "| 阶段门槛 | 状态 |", "|---|---|"]
    lines.extend(f"| {name} | {'PASS' if good else 'NOT_MET'} |"
                 for name, good in gates.items())
    lines += ["", "失败种子与完整命令/网表/日志位于相应 suite 的 evidence.tar.gz。", ""]
    save(args.out / "summary.md", "\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
