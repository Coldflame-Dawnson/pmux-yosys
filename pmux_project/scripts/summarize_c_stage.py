#!/usr/bin/env python3
"""Make the C stage gate explicit from immutable run outputs."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--random", type=Path, default=ROOT/"reports"/"c-real-random-1000")
    parser.add_argument("--baseline", type=Path, default=ROOT/"reports"/"c-official-baseline-check")
    parser.add_argument("--optimized", type=Path, default=ROOT/"reports"/"c-official-real-split")
    parser.add_argument("--out", type=Path, default=ROOT/"reports"/"c-stage-gate")
    args=parser.parse_args()
    random=read(args.random/"summary.json")
    baseline=read(args.baseline/"summary.json")
    optimized=read(args.optimized/"summary.json")
    random_ok=(random.get("mode")=="external" and random.get("external_kind")=="real"
               and random.get("count")==random.get("pass")==1000 and random.get("fail")==0
               and all(random.get(key)=="PASS" for key in ("structure_correct","synthesis_pass","eqy_pass")))
    base_ok=(baseline.get("mode")=="identity" and baseline.get("synthesis_eqy")=="PASS"
             and len(baseline.get("cases",[]))==4 and all(not case["issues"] and
             all(value==0 for value in case["delta"].values()) for case in baseline["cases"]))
    opt_ok=(optimized.get("mode")=="real" and len(optimized.get("cases",[]))==4
            and optimized.get("structure_correct")==optimized.get("eqy_pass")=="PASS"
            and all(case.get("pass_location")=="PASS" and case.get("synthesis")=="PASS"
                    and not case["issues"] for case in optimized["cases"]))
    reasons=[]
    if not random_ok: reasons.append("1000_real_random_failed")
    if not base_ok: reasons.append("official_baseline_reproduction_failed")
    if not opt_ok: reasons.append("official_optimized_structure_or_eqy_failed")
    if optimized.get("lut_benefit")!="PASS": reasons.append("official_lut_benefit_failed")
    if optimized.get("production_yosys_loading_confirmed")!="RUN_VERIFIED":
        reasons.append("production_yosys_loading_pending_B")
    rows=[]
    for original, current in zip(baseline["cases"],optimized["cases"]):
        if original["case"]!=current["case"] or original["input_sha256"]!=current["input_sha256"]:
            reasons.append("official_case_or_input_hash_mismatch")
        rows.append({"case":current["case"],"input_sha256":current["input_sha256"],
                     "baseline":{"lut":original.get("lut"),"dff":original.get("dff"),"io":original.get("io"),
                                 "cpu_s":original.get("cpu_s"),"peak_memory_mb":original.get("peak_memory_mb")},
                     "optimized":{"lut":current.get("lut"),"dff":current.get("dff"),"io":current.get("io"),
                                  "cpu_s":current.get("cpu_s"),"peak_memory_mb":current.get("peak_memory_mb"),
                                  "changed_bits":current.get("changed_bits"),"structure":current.get("structure"),
                                  "pass_location":current.get("pass_location"),"eqy":current.get("eqy"),
                                  "eqy_method":current.get("eqy_method"),"abc_internal_error":current.get("abc_internal_error")}})
    report={"schema":1,"random_1000":{"count":random["count"],"pass":random["pass"],"fail":random["fail"],
                                       "structure_correct":random["structure_correct"],
                                       "synthesis_pass":random["synthesis_pass"],"eqy_pass":random["eqy_pass"]},
            "official_baseline_reproduced":base_ok,"official_optimized_flow":optimized.get("flow"),
            "official_cases":rows,"official_structure_correct":optimized.get("structure_correct"),
            "official_eqy_pass":optimized.get("eqy_pass"),"official_lut_benefit":optimized.get("lut_benefit"),
            "production_loading":optimized.get("production_yosys_loading_confirmed"),
            "blockers":sorted(set(reasons)),"ready_to_merge_integration_g3":not reasons,
            "evidence_roots":{"random":str(args.random.resolve()),"baseline":str(args.baseline.resolve()),
                              "optimized":str(args.optimized.resolve())}}
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/"summary.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    lines=["# C 阶段验收", "",f"固定种子：{random['pass']}/1000；结构、综合、EQY：{random['structure_correct']}/{random['synthesis_pass']}/{random['eqy_pass']}。",
           f"官方基线重现：{'PASS' if base_ok else 'FAIL'}；优化结构：{optimized.get('structure_correct')}；EQY：{optimized.get('eqy_pass')}；LUT 收益：{optimized.get('lut_benefit')}。",
           f"生产版加载验证：{optimized.get('production_yosys_loading_confirmed')}；允许合入 integration/g3：{'YES' if not reasons else 'NO'}。","",
           "| Case | LUT | DFF | IO | CPU s | Peak MB | 改写位 | EQY 路径 | ABC 内部报错 |",
           "|---|---:|---:|---:|---:|---:|---:|---|---|"]
    for row in rows:
        base,opt=row["baseline"],row["optimized"]
        lines.append(f"| {row['case']} | {base['lut']}→{opt['lut']} | {base['dff']}→{opt['dff']} | {base['io']}→{opt['io']} | {base['cpu_s']}→{opt['cpu_s']} | {base['peak_memory_mb']}→{opt['peak_memory_mb']} | {opt['changed_bits']} | {opt['eqy_method']} | {opt['abc_internal_error']} |")
    lines.extend(["","阻塞："+(", ".join(report["blockers"]) if reasons else "无"),"",
                  "分段综合按冻结的 map_ffram 顺序调用真实插件；完整综合及后 ABC9 各执行一次 PMUX_OPT。生产版加载方式由 B 确认后用 `--real-flow integrated` 重跑。", ""])
    (args.out/"summary.md").write_text("\n".join(lines),encoding="utf-8")
    print(f"random={random['pass']}/1000 official_lut={optimized.get('lut_benefit')} ready_to_merge={not reasons}")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
