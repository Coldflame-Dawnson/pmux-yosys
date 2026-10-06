#!/usr/bin/env python3
"""Summarize M1 framework runs without promoting stub results to real Pass results."""
import json
from pathlib import Path
from run_micro import ROOT, save

REPORTS=ROOT/"reports"

def read(name):
    p=REPORTS/name/"summary.json"
    return json.loads(p.read_text()) if p.is_file() else None

def main():
    identity=read("c-m1-micro-identity")
    stub=read("c-m1-micro-stub")
    external_stub=read("c-m1-external-stub")
    b_unknown=read("c-m1-b-unknown")
    random=read("c-m1-random")
    official=read("c-m1-official")
    report={"schema":1,"runs_present":{k:v is not None for k,v in
            (("micro_identity",identity),("micro_stub",stub),("external_stub",external_stub),
             ("b_unknown",b_unknown),("random_100",random),("official_4",official))},
            "framework_structure":"PASS" if stub and len(stub["rows"])==19 and stub["structure_correct"]=="PASS" else "NOT_RUN",
            "framework_eqy":"PASS" if stub and len(stub["rows"])==19 and stub["eqy_pass"]=="PASS" else "NOT_RUN",
            "external_adapter":"PASS" if external_stub and len(external_stub["rows"])==19 and
                                 external_stub["structure_correct"]=="PASS" and external_stub["eqy_pass"]=="PASS" else "NOT_RUN",
            "b_unknown_stub":"PASS" if b_unknown and len(b_unknown["rows"])==19 and
                              b_unknown["structure_correct"]=="PASS" and b_unknown["eqy_pass"]=="PASS" else "NOT_RUN",
            "random_identity":"PASS" if random and random["count"]==100 and random["fail"]==0 else "NOT_RUN",
            "official_identity":"PASS" if official and len(official["cases"])==4 and official["eqy_pass"]=="PASS" else "NOT_RUN",
            "real_pass_structure":"NOT_RUN","real_pass_eqy":"NOT_RUN","lut_benefit":"NOT_RUN",
            "limits":["B forced stub has no per-bit machine plan; A proof engine unavailable", "B reason-code/log schema not frozen",
                      "event-budget fixture needs A counter calibration", "1000 seeds reserved for stage acceptance"]}
    save(REPORTS/"c-m1-summary.json",json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    lines=["# C 部分 M1 汇总","",f"框架结构检查：{report['framework_structure']}；框架 EQY：{report['framework_eqy']}。",
           f"真实 Pass 结构改写正确：{report['real_pass_structure']}；真实 Pass EQY 通过：{report['real_pass_eqy']}；LUT 收益通过：{report['lut_benefit']}。","",
           f"微型用例 19；外部桩适配 {report['external_adapter']}；B UNKNOWN 桩 {report['b_unknown_stub']}；随机快速回归 {random['count'] if random else 0}；官方 case {len(official['cases']) if official else 0}。",
           "", "Fixture replay 是测试设施联通，不是 B 桩或 A 的证明。", "", "## 待办",""]
    lines += ["- "+x for x in report["limits"]]
    save(REPORTS/"c-m1-summary.md","\n".join(lines)+"\n")
    print(json.dumps({k:v for k,v in report.items() if k not in ("limits","runs_present")},ensure_ascii=False))

if __name__=="__main__":main()
