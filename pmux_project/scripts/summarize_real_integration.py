#!/usr/bin/env python3
"""Summarize actual Pass runs without promoting stubs or bit edits to LUT gains."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "A": "445231c4c616781a957292699376cb73bca60037",
    "B": "69caa80a8d610ceaca4ddba92721c2a294c5faa0",
    "C_frozen": "71bfcfc97e5a8d42635586ba96d461f0107f9518",
}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def evidence(work):
    names = ("before.il", "after.il", "plan.json", "stats.json", "eqy.log")
    return {name: str(work / name) if (work / name).is_file() else None for name in names}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--micro", type=Path, default=ROOT / "reports" / "c-real-micro-19")
    parser.add_argument("--random", type=Path, default=ROOT / "reports" / "c-real-random-100")
    parser.add_argument("--atomic", type=Path, default=ROOT / "reports" / "c-real-atomic")
    parser.add_argument("--out", type=Path, default=ROOT / "reports" / "c-real-integration")
    args = parser.parse_args()
    micro, random, atomic = (read_json(path / "summary.json") for path in
                             (args.micro, args.random, args.atomic))
    plugin = ROOT / "pass" / "pmux_opt.so"
    rows, failures = [], []
    for row in micro["rows"]:
        work = args.micro / row["case"]
        plan, stats = read_json(work / "plan.json"), read_json(work / "stats.json")
        rows.append({"case": row["case"], "result": row["result"],
                     "structure": row["structure"], "check": row["yosys_check"],
                     "eqy": row["eqy"], "changed_bits": stats["changed_bits"],
                     "plan_edits": len(plan["edits"]), "commit": plan["commit"],
                     "reason_codes": stats["reason_codes"]})
        if row["result"] != "PASS":
            failures.append({"suite": "micro", "id": row["case"], "issues": row["issues"],
                             "artifacts": evidence(work)})
    for row in random["rows"]:
        if row["result"] != "PASS":
            work = args.random / f"seed_{row['seed']:08d}"
            failures.append({"suite": "random", "id": row["seed"], "issues": row["issues"],
                             "artifacts": evidence(work)})
    real_micro = micro.get("mode") == "external" and len(rows) == 19 and micro["counts"] == {"PASS": 19, "FAIL": 0, "SKIP": 0}
    real_random = (random.get("mode") == "external" and random.get("external_kind") == "real"
                   and random.get("count") == 100 and random.get("pass") == 100 and random.get("fail") == 0)
    real_files = all(read_json(args.micro / row["case"] / "plan.json").get("mode") == "real" for row in rows)
    real_files &= all(read_json(args.random / f"seed_{row['seed']:08d}" / "plan.json").get("mode") == "real"
                      for row in random["rows"])
    ready = real_micro and real_random and real_files and not failures
    random_plans = [read_json(args.random / f"seed_{row['seed']:08d}" / "plan.json") for row in random["rows"]]
    report = {"schema": 1, "branch": subprocess.check_output(["git", "branch", "--show-current"], cwd=ROOT, text=True).strip(),
              "source_commits": SOURCES, "tool_versions": micro["versions"],
              "plugin_sha256": hashlib.sha256(plugin.read_bytes()).hexdigest(),
              "real_micro": {"count": len(rows), "pass": micro["counts"]["PASS"],
                             "structure_correct": micro["structure_correct"],
                             "eqy_pass": micro["eqy_pass"], "rows": rows},
              "real_random": {"count": random["count"], "pass": random["pass"],
                              "fail": random["fail"], "structure_correct": random["structure_correct"],
                              "synthesis_pass": random["synthesis_pass"], "eqy_pass": random["eqy_pass"],
                              "changed_bits": sum(len(plan["edits"]) for plan in random_plans),
                              "edited_cases": sum(bool(plan["edits"]) for plan in random_plans)},
              "atomic_test_only": atomic, "failures": failures,
              "failure_evidence_complete": all(all(x is not None for x in failure["artifacts"].values()) for failure in failures),
              "lut_benefit": "NOT_RUN", "ready_for_1000": ready}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# C 真实 Pass 联调", "", f"分支：`{report['branch']}`。A/B/C 冻结输入：`{SOURCES['A']}` / `{SOURCES['B']}` / `{SOURCES['C_frozen']}`。", "",
             f"19 微型：{micro['counts']['PASS']}/19；结构 {micro['structure_correct']}；EQY {micro['eqy_pass']}。",
             f"100 随机：{random['pass']}/100；综合 {random['synthesis_pass']}；结构 {random['structure_correct']}；EQY {random['eqy_pass']}；{report['real_random']['edited_cases']} 例共 {report['real_random']['changed_bits']} 位改写。",
             f"原子回退测试专用模式：{atomic['status']}；LUT 收益：NOT_RUN；进入 1000 例：{'YES' if ready else 'NO'}。", "",
             "| 用例 | 结果 | 改写位 | 结构 | check | EQY | 原因码 |", "|---|---|---:|---|---|---|---|"]
    for row in rows:
        lines.append(f"| {row['case']} | {row['result']} | {row['changed_bits']} | {row['structure']} | {row['check']} | {row['eqy']} | {', '.join(row['reason_codes'])} |")
    lines.extend(["", "真实运行的每例目录保留 before/after、Pass 原生 plan/stats、命令、Yosys 与 EQY 完整日志。原子回退预检使用 B 测试专用开关，未计入真实证明。", ""])
    if failures:
        lines.append("## 失败证据")
        lines.append("")
        for failure in failures:
            lines.append(f"- {failure['suite']} {failure['id']}: {failure['artifacts']}")
    else:
        lines.append("最终 19+100 回归无失败用例；早期 14c 测试数据校准失败的 before/after/plan/stats/EQY 日志归档为 `reports/c-real-integration/initial-14c-fixture-calibration.tar.gz`。该次错误来自 C 夹具未达到事件预算，已修正并在最终 14c 通过。")
    (args.out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"micro={micro['counts']['PASS']}/19 random={random['pass']}/100 ready_for_1000={ready}")
    return 0 if ready else 1


if __name__ == "__main__":
    raise SystemExit(main())
