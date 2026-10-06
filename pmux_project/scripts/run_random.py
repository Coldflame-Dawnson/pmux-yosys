#!/usr/bin/env python3
"""Fixed-seed random MUX/PMUX synthesis and EQY regression."""
import argparse
import json
import os
import random
import shlex
import subprocess
import sys
from pathlib import Path

from generate_micro import Design
from run_micro import DEFAULT_BIN, ROOT, compare, run, save, validate_machine, yosys_json

SEEDS = ROOT / "tests" / "pmux_m1" / "seeds_1000.txt"


def design(seed):
    r = random.Random(seed)
    d = Design()
    x = d.wire("x", 4, "input")
    y = d.wire("y", 4, "input")
    z = d.wire("z", 4, "input")
    s = d.wire("s", 2, "input")
    pool = [x, y, z]
    for i in range(r.randrange(1, 5)):
        op = r.choice(("and", "or", "xor", "not"))
        a = r.choice(pool)
        b = r.choice(pool) if op != "not" else None
        pool.append(d.logic(op, a, b, 4, f"logic{i}"))
    select = d.compare("eq", x, r.randrange(16), 4) if r.randrange(2) else "\\s [0]"
    a, b = r.sample(pool, 2)
    if r.randrange(2): d.mux(a, b, select, 4)
    else:
        branch = r.choice(pool)
        d.pmux(a, "{ " + branch + " " + b + " }", "\\s", 4, 2)
    return d.render()


def eqy_config(before, after, module):
    internal = sorted(set(module["cells"]) | (set(module["netnames"]) - set(module["ports"])))
    rules = "".join(f"gold-nomatch {n}\ngate-nomatch {n}\n" for n in internal)
    return (f"[gold]\nread_rtlil {before}\n[gate]\nread_rtlil {after}\n"
            f"[script]\nhierarchy -check -top top\nprep\n[match top]\n{rules}"
            "[collect top]\ngroup *\nbind *\n[strategy prove]\nuse sby\nengine abc pdr\noption aigsmt none\n")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--count", type=int, choices=(100,1000), default=100)
    p.add_argument("--seed", type=int, help="reproduce exactly one seed")
    p.add_argument("--mode", choices=("identity","external"), default="identity")
    p.add_argument("--external-kind", choices=("untriggered", "stub", "real"), default="untriggered")
    p.add_argument("--dut-template")
    p.add_argument("--bin",type=Path,default=DEFAULT_BIN)
    p.add_argument("--out",type=Path,default=ROOT/"reports"/"c-m1-random")
    a=p.parse_args()
    a.out=a.out.resolve()
    if a.mode=="external" and not a.dut_template: p.error("--dut-template required")
    if a.mode=="identity" and a.external_kind=="real": p.error("real external kind requires external mode")
    all_seeds=[int(s) for s in SEEDS.read_text().splitlines() if s.strip()]
    if len(all_seeds)!=1000 or len(set(all_seeds))!=1000: p.error("seed inventory invalid")
    selected=[a.seed] if a.seed is not None else all_seeds[:a.count]
    yosys,eqy=a.bin/"yosys",a.bin/"eqy"
    env=os.environ.copy(); env["PATH"]=str(a.bin)+os.pathsep+env.get("PATH","")
    versions={name:subprocess.run([a.bin/name,"-V" if name=="yosys" else "--version"],capture_output=True,text=True).stdout.strip() for name in ("yosys","eqy")}
    rows=[]
    for seed in selected:
        w=a.out/f"seed_{seed:08d}"; w.mkdir(parents=True,exist_ok=True)
        before,after=w/"before.il",w/"after.il"
        save(before,design(seed))
        row={"seed":seed,"synthesis":"NOT_RUN","structure":"NOT_RUN","eqy":"NOT_RUN",
             "proof_engine":"NOT_RUN" if a.external_kind != "real" else "REAL_RUN",
             "result":"FAIL","issues":[]}
        try:
            old=yosys_json(before,w/"before.json",yosys,w,env,"before_json")
            plan_path,stats_path=w/"plan.json",w/"stats.json"
            if a.mode=="identity":
                save(after,before.read_text());save(plan_path,'{"schema":1,"edits":[]}\n');save(stats_path,'{"changed_bits":0}\n')
            else:
                vars={"before":str(before),"after":str(after),"plan":str(plan_path),"stats":str(stats_path),"seed":seed,"project":str(ROOT)}
                cmd=[v.format(**vars) for v in shlex.split(a.dut_template)]
                if run(cmd,w/"dut.log",w,env,timeout=900 if a.external_kind=="real" else 300): raise RuntimeError("DUT failed")
            new=yosys_json(after,w/"after.json",yosys,w,env,"after_json")
            plan=json.loads(plan_path.read_text());stats=json.loads(stats_path.read_text())
            spec={"expected_edits":plan.get("edits",[])}
            issues,count=compare(old,new,spec,plan,True)
            if a.external_kind=="real": issues+=validate_machine(old,new,plan,stats,count)
            elif count!=stats.get("changed_bits"): issues.append("changed_bits statistic mismatch")
            row["structure"]="FAIL" if issues else "PASS";row["issues"]+=issues
            check=f'read_rtlil "{after}"; hierarchy -check -top top; check; synth_intel -family cycloneiv -top top'
            rc=run([yosys,"-Q","-p",check],w/"synthesis.log",w,env,timeout=180)
            row["synthesis"]="PASS" if rc==0 and "0 problems" in (w/"synthesis.log").read_text() else "FAIL"
            cfg=w/"verify.eqy";save(cfg,eqy_config(before,after,old["top"]))
            rc=run([eqy,"-f","-d",w/"eqy_work",cfg],w/"eqy.log",w,env,timeout=600 if a.external_kind=="real" else 180)
            row["eqy"]="PASS" if rc==0 and "DONE (PASS" in (w/"eqy.log").read_text() else "FAIL"
            row["result"]="PASS" if all(row[k]=="PASS" for k in ("structure","synthesis","eqy")) else "FAIL"
        except Exception as e: row["issues"].append(str(e))
        if row["result"]=="FAIL":
            save(w/"failure.json",json.dumps({"seed":seed,"versions":versions,"issues":row["issues"],
                "reproduce":f"python3 pmux_project/scripts/run_random.py --seed {seed} --mode {a.mode} --external-kind {a.external_kind} --bin {a.bin} --out {a.out} --dut-template {shlex.quote(a.dut_template or '')}"},indent=2)+"\n")
        rows.append(row)
        print(f"seed {seed}: {row['result']} synth={row['synthesis']} structure={row['structure']} eqy={row['eqy']}",flush=True)
    summary={"schema":1,"mode":a.mode,"external_kind":a.external_kind,"versions":versions,"count":len(rows),"pass":sum(x["result"]=="PASS" for x in rows),
             "fail":sum(x["result"]=="FAIL" for x in rows),"rows":rows,"lut_benefit":"NOT_RUN"}
    summary["structure_correct"]="PASS" if all(x["structure"]=="PASS" for x in rows) else "FAIL"
    summary["eqy_pass"]="PASS" if all(x["eqy"]=="PASS" for x in rows) else "FAIL"
    summary["synthesis_pass"]="PASS" if all(x["synthesis"]=="PASS" for x in rows) else "FAIL"
    save(a.out/"summary.json",json.dumps(summary,indent=2)+"\n")
    save(a.out/"summary.md",f"# 随机回归\n\n模式：{a.mode}/{a.external_kind}；综合/结构/EQY PASS {summary['pass']}；FAIL {summary['fail']}；证明引擎 {'REAL_RUN' if a.external_kind=='real' else 'NOT_RUN'}；LUT 收益 NOT_RUN。\n\n每个 seed 目录保存原始设计、命令、版本、完整日志及最小化前样本。\n")
    return 1 if summary["fail"] else 0


if __name__=="__main__":sys.exit(main())
