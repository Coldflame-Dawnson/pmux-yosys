#!/usr/bin/env python3
"""Official case regression using frozen synth_intel/ABC9 and baseline EQY scope."""
import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from run_micro import DEFAULT_BIN, ROOT, run, save

BASE = ROOT / "official_baseline"
HASHES = [
    "2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13",
    "e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146",
    "1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9",
    "c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7",
]


def config(src, gate, top, whole):
    body = (f"[gold]\nread_verilog -formal {src}\n\n[gate]\nread_rtlil {gate}\n"
            f"read_verilog -formal -overwrite {BASE/'environment'/'dffeas_formal.v'}\n\n"
            f"[script]\nhierarchy -check -top {top}\nprep\nmemory_map\nasync2sync\n\n")
    if whole:
        body += f"[partition {top}]\nmerge *\n\n"
    body += "[strategy sat]\nuse sat\ndepth 20\n"
    if whole: body += "\n[strategy pdr]\nuse sby\nengine abc pdr\ntimeout 300\n"
    return body


def metrics(path, top, out, env):
    script=BASE/"scripts"/"analyze_netlist.py"
    p=subprocess.run([sys.executable,str(script),"--json",str(path),"--top",top,"--output",str(out),"--validate-dffeas"],
                     text=True,capture_output=True,env=env)
    if p.returncode: raise RuntimeError(f"metric scan failed: {p.stdout} {p.stderr}")
    return json.loads(out.read_text())


def timing(log):
    matches=re.findall(r"End of script\..*?user:\s*([0-9.]+)s, system:\s*([0-9.]+)s, MEM:\s*([0-9.]+) MB peak",log,re.S)
    if not matches: raise RuntimeError("missing full synthesis CPU/memory")
    u,s,m=map(float,matches[-1]);return round(u+s,3),m


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--mode",choices=("identity","real"),default="identity")
    p.add_argument("--bin",type=Path,default=DEFAULT_BIN)
    p.add_argument("--case",type=int,choices=(1,2,3,4),action="append")
    p.add_argument("--out",type=Path,default=ROOT/"reports"/"c-m1-official")
    a=p.parse_args()
    a.out=a.out.resolve()
    baseline={r["case"]:r for r in csv.DictReader((BASE/"results"/"baseline.csv").open(encoding="utf-8-sig"))}
    yosys,eqy=a.bin/"yosys",a.bin/"eqy"
    if not yosys.is_file() or not eqy.is_file(): p.error(f"missing tools: {a.bin}")
    env=os.environ.copy();env["PATH"]=str(a.bin)+os.pathsep+env.get("PATH","")
    versions={"yosys":subprocess.run([yosys,"-V"],capture_output=True,text=True).stdout.strip(),
              "eqy":subprocess.run([eqy,"--version"],capture_output=True,text=True).stdout.strip()}
    cases=a.case or [1,2,3,4]
    rows=[]
    for i in cases:
        top=f"test{i}";case=f"case{i}";w=a.out/case;w.mkdir(parents=True,exist_ok=True)
        src=ROOT/"pmux_case"/"competition_case"/f"{top}.v"
        row={"case":case,"source":str(src),"input_sha256":"","synthesis":"NOT_RUN","eqy":"NOT_RUN",
             "eqy_method":"NOT_RUN","lut_benefit":"NOT_RUN","structure":"NOT_RUN","issues":[]}
        try:
            digest=hashlib.sha256(src.read_bytes()).hexdigest();row["input_sha256"]=digest
            if digest!=HASHES[i-1]: raise RuntimeError("official source SHA-256 mismatch")
            for stage in ("post_abc9","full"):
                net=w/f"{stage}.json"
                command=(f'read_verilog "{src}"; synth_intel -family cycloneiv -top {top}' +
                         (" -run begin:map_cells" if stage=="post_abc9" else "") +
                         f'; stat; write_json "{net}"')
                if stage=="post_abc9": command+=f'; select {top}; write_rtlil -selected "{w/"post_abc9.il"}"'
                save(w/f"{stage}.ys",command+"\n")
                rc=run([yosys,"-l",w/f"{stage}.log","-s",w/f"{stage}.ys"],w/f"{stage}.console.log",w,env,timeout=600)
                if rc: raise RuntimeError(f"{stage} synthesis rc={rc}")
            data=metrics(w/"post_abc9.json",top,w/"metrics.json",env)
            cpu,mem=timing((w/"full.log").read_text())
            row.update({"lut":data["lut_count"],"dff":data["dff_count"],"io":data["io_count"],
                        "cpu_s":cpu,"peak_memory_mb":mem,"synthesis":"PASS"})
            full=(w/"full.log").read_text()
            row["abc_internal_error"]="ABC: Error: Abc_FrameUpdateGia(): Transformation has failed." in full
            if data["lut_count"]<=0: raise RuntimeError("no ABC9 $lut cells")
            for method in ("default","whole"):
                cfg=w/f"{method}.eqy";save(cfg,config(src,w/"post_abc9.il",top,method=="whole"))
                rc=run([eqy,"-f","-d",w/f"eqy_{method}",cfg],w/f"eqy_{method}.log",w,env,timeout=600)
                passed=rc==0 and "DONE (PASS" in (w/f"eqy_{method}.log").read_text()
                row[f"eqy_{method}"]="PASS" if passed else "FAIL"
                if passed:
                    row["eqy"]="PASS";row["eqy_method"]="whole_design" if method=="whole" else "default_partitions"
                    break
            if row["eqy"]!="PASS": raise RuntimeError("both EQY methods failed")
            base=baseline[case]
            row["baseline"]={k:base[k] for k in ("lut_count","dff_count","io_count","cpu_time","peak_memory","eqy_status")}
            row["delta"]={k:row[k]-int(base[src_key]) for k,src_key in (("lut","lut_count"),("dff","dff_count"),("io","io_count"))}
            if a.mode=="real": row["lut_benefit"]="PASS" if row["delta"]["lut"]<=0 and row["delta"]["dff"]<=0 and row["delta"]["io"]<=0 else "FAIL"
        except Exception as e: row["issues"].append(str(e))
        rows.append(row)
        print(f"{case}: synth={row['synthesis']} EQY={row['eqy']} method={row['eqy_method']} LUT={row.get('lut','?')} benefit={row['lut_benefit']}",flush=True)
    all_ok=all(r["synthesis"]=="PASS" and r["eqy"]=="PASS" for r in rows)
    benefit=("PASS" if len(rows)==4 and all_ok and all(r["lut_benefit"]=="PASS" for r in rows) and any(r["delta"]["lut"]<0 for r in rows) else "NOT_RUN" if len(rows)<4 else "FAIL") if a.mode=="real" else "NOT_RUN"
    result={"schema":1,"mode":a.mode,"versions":versions,"cases":rows,"synthesis_eqy":"PASS" if all_ok else "FAIL",
            "structure_correct":"NOT_RUN","eqy_pass":"PASS" if all_ok else "FAIL","lut_benefit":benefit,
            "note":"Known baseline ABC internal error is recorded per case; it is not silently discarded."}
    save(a.out/"summary.json",json.dumps(result,ensure_ascii=False,indent=2)+"\n")
    lines=[f"# 官方四例回归 ({a.mode})","",f"结构改写：NOT_RUN；EQY：{result['eqy_pass']}；LUT 收益：{benefit}。","",
           "| Case | LUT | ΔLUT | DFF | IO | CPU s | Peak MB | EQY | Method | ABC error |",
           "|---|---:|---:|---:|---:|---:|---:|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['case']} | {r.get('lut','')} | {r.get('delta',{}).get('lut','')} | {r.get('dff','')} | {r.get('io','')} | {r.get('cpu_s','')} | {r.get('peak_memory_mb','')} | {r['eqy']} | {r['eqy_method']} | {r.get('abc_internal_error','')} |")
    save(a.out/"summary.md","\n".join(lines)+"\n")
    return 0 if all(r["synthesis"]=="PASS" and r["eqy"]=="PASS" for r in rows) else 1


if __name__=="__main__":sys.exit(main())
