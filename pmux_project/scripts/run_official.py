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

from run_micro import DEFAULT_BIN, ROOT, compare, run, save, validate_machine

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


def synth_script(src, top, stage, mode, real_flow, plugin, work):
    """Reproduce synth_intel's map_ffram sequence with one Pass insertion."""
    if mode == "identity":
        return (f'read_verilog "{src}"; synth_intel -family cycloneiv -top {top}' +
                (" -run begin:map_cells" if stage == "post_abc9" else ""))
    if real_flow == "integrated":
        return (f'plugin -i "{plugin}"; read_verilog "{src}"; '
                f'synth_intel -family cycloneiv -top {top}' +
                (" -run begin:map_cells" if stage == "post_abc9" else ""))
    script = (f'plugin -i "{plugin}"; read_verilog "{src}"; '
              f'synth_intel -family cycloneiv -top {top} -run begin:map_ffram; '
              'opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; '
              'log PMUX_OPT_ANCHOR_AFTER_OPT_UNDRIVEN_FINE; pmux_opt; '
              'log PMUX_OPT_ANCHOR_BEFORE_COMMON_TECHMAP; '
              'techmap -map +/techmap.v; opt -full; clean -purge; setundef -undriven -zero; '
              f'synth_intel -family cycloneiv -top {top} '
              f'-run map_ffs:{"map_cells" if stage == "post_abc9" else ""}')
    return script


def diagnostic_script(src, top, plugin, work):
    return (f'plugin -i "{plugin}"; read_verilog "{src}"; '
            f'synth_intel -family cycloneiv -top {top} -run begin:map_ffram; '
            'opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; '
            f'write_json "{work / "before_pmux.json"}"; '
            f'write_rtlil "{work / "before_pmux.il"}"; '
            f'pmux_opt -write-plan "{work / "plan.json"}" -write-stats "{work / "stats.json"}"; '
            f'write_json "{work / "after_pmux.json"}"; '
            f'write_rtlil "{work / "after_pmux.il"}"; check')


def pass_location(log, expected_writes, real_flow):
    anchor1 = "PMUX_OPT_ANCHOR_AFTER_OPT_UNDRIVEN_FINE"
    header = "Executing PMUX_OPT guarded analysis and atomic rewrite."
    anchor2 = "PMUX_OPT_ANCHOR_BEFORE_COMMON_TECHMAP"
    techmap = "Executing TECHMAP pass"
    if log.count(header) != 1:
        return ["PMUX_OPT did not occur exactly once"]
    position = log.index(header)
    if real_flow == "split":
        if log.count(anchor1) != 1 or log.count(anchor2) != 1:
            return ["PMUX_OPT position anchors did not occur exactly once"]
        positions = [log.index(marker) for marker in (anchor1, header, anchor2)]
        next_techmap = log.find(techmap, positions[-1])
        if positions != sorted(positions) or next_techmap < 0:
            return ["PMUX_OPT is not after fine opt and before common techmap"]
    elif log.rfind("Executing OPT pass", 0, position) < 0 or log.find(techmap, position) < 0:
        return ["integrated PMUX_OPT is not bracketed by opt and techmap"]
    writes = re.findall(r"pmux_opt: rewrite .*?writes=(\d+) commit=", log)
    return [] if len(writes) == 1 and int(writes[0]) == expected_writes else ["PMUX_OPT write count differs from diagnostic plan"]


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--mode",choices=("identity","real"),default="identity")
    p.add_argument("--real-flow",choices=("split","integrated"),default="split",
                   help="split: baseline Yosys with exact map_ffram insertion; integrated: B's patched Yosys")
    p.add_argument("--bin",type=Path,default=DEFAULT_BIN)
    p.add_argument("--plugin",type=Path,default=ROOT/"pass"/"pmux_opt.so")
    p.add_argument("--source-root",type=Path,default=ROOT/"pmux_case"/"competition_case")
    p.add_argument("--case",type=int,choices=(1,2,3,4),action="append")
    p.add_argument("--out",type=Path,default=ROOT/"reports"/"c-m1-official")
    a=p.parse_args()
    a.out=a.out.resolve()
    a.plugin=a.plugin.resolve()
    a.source_root=a.source_root.resolve()
    if a.mode=="real" and not a.plugin.is_file(): p.error(f"missing Pass plugin: {a.plugin}")
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
        src=a.source_root/f"{top}.v"
        row={"case":case,"source":str(src),"input_sha256":"","synthesis":"NOT_RUN","eqy":"NOT_RUN",
             "eqy_method":"NOT_RUN","lut_benefit":"NOT_RUN","structure":"NOT_RUN",
             "pass_location":"NOT_RUN","issues":[]}
        try:
            digest=hashlib.sha256(src.read_bytes()).hexdigest();row["input_sha256"]=digest
            if digest!=HASHES[i-1]: raise RuntimeError("official source SHA-256 mismatch")
            if a.mode=="real":
                diagnostic=diagnostic_script(src,top,a.plugin,w)
                save(w/"diagnostic.ys",diagnostic+"\n")
                rc=run([yosys,"-l",w/"diagnostic.log","-s",w/"diagnostic.ys"],
                       w/"diagnostic.console.log",w,env,timeout=600)
                if rc: raise RuntimeError(f"diagnostic Pass rc={rc}")
                before=json.loads((w/"before_pmux.json").read_text())["modules"]
                after=json.loads((w/"after_pmux.json").read_text())["modules"]
                plan=json.loads((w/"plan.json").read_text())
                stats=json.loads((w/"stats.json").read_text())
                issues,count=compare(before,after,{"expected_edits":plan.get("edits",[])},plan,True)
                issues+=validate_machine(before,after,plan,stats,count)
                row["structure"]="PASS" if not issues else "FAIL"
                row["issues"].extend(issues)
                row["changed_bits"]=count
                row["commit"]=plan.get("commit")
                row["reason_codes"]=stats.get("reason_codes",[])
                if issues: raise RuntimeError("diagnostic Pass plan/structure mismatch")
            for stage in ("post_abc9","full"):
                net=w/f"{stage}.json"
                command=synth_script(src,top,stage,a.mode,a.real_flow,a.plugin,w)+f'; stat; write_json "{net}"'
                if stage=="post_abc9": command+=f'; select {top}; write_rtlil -selected "{w/"post_abc9.il"}"'
                save(w/f"{stage}.ys",command+"\n")
                rc=run([yosys,"-l",w/f"{stage}.log","-s",w/f"{stage}.ys"],w/f"{stage}.console.log",w,env,timeout=600)
                if rc: raise RuntimeError(f"{stage} synthesis rc={rc}")
                if a.mode=="real":
                    issues=pass_location((w/f"{stage}.log").read_text(),row["changed_bits"],a.real_flow)
                    row["issues"].extend(issues)
                    if issues: raise RuntimeError(f"{stage}: PMUX_OPT location/count failed")
            if a.mode=="real": row["pass_location"]="PASS"
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
            if a.mode=="identity" and any(row["delta"].values()):
                raise RuntimeError("baseline reproduction differs in LUT/DFF/IO")
            if a.mode=="real":
                row["resource_nonincrease"]="PASS" if all(value<=0 for value in row["delta"].values()) else "FAIL"
                row["lut_benefit"]="PASS" if row["resource_nonincrease"]=="PASS" and row["delta"]["lut"]<0 else "NO_GAIN" if row["resource_nonincrease"]=="PASS" else "FAIL"
        except Exception as e: row["issues"].append(str(e))
        rows.append(row)
        print(f"{case}: synth={row['synthesis']} EQY={row['eqy']} method={row['eqy_method']} LUT={row.get('lut','?')} benefit={row['lut_benefit']}",flush=True)
    all_ok=all(r["synthesis"]=="PASS" and r["eqy"]=="PASS" and not r["issues"] for r in rows)
    benefit=("PASS" if len(rows)==4 and all_ok and all(r.get("resource_nonincrease")=="PASS" for r in rows) and any(r["lut_benefit"]=="PASS" for r in rows) else "NOT_RUN" if len(rows)<4 else "FAIL") if a.mode=="real" else "NOT_RUN"
    location_ok=all(r["pass_location"]=="PASS" for r in rows)
    result={"schema":1,"mode":a.mode,"versions":versions,"cases":rows,"synthesis_eqy":"PASS" if all_ok else "FAIL",
            "structure_correct":"PASS" if a.mode=="real" and location_ok and all(r["structure"]=="PASS" for r in rows) else "NOT_RUN" if a.mode=="identity" else "FAIL",
            "eqy_pass":"PASS" if all(r["eqy"]=="PASS" for r in rows) else "FAIL","lut_benefit":benefit,
            "flow":("source_matched_split_stage_plugin" if a.real_flow=="split" else "patched_synth_intel") if a.mode=="real" else "frozen_baseline",
            "production_yosys_loading_confirmed":"NOT_RUN" if a.mode=="identity" else "PENDING_B_CONFIRMATION" if a.real_flow=="split" else "RUN_VERIFIED" if location_ok and all_ok else "FAIL",
            "note":"Known baseline ABC internal error is recorded per case; it is not silently discarded."}
    save(a.out/"summary.json",json.dumps(result,ensure_ascii=False,indent=2)+"\n")
    lines=[f"# 官方四例回归 ({a.mode})","",f"结构改写：{result['structure_correct']}；EQY：{result['eqy_pass']}；LUT 收益：{benefit}。","",
           ("真实模式使用与冻结 synth_intel.cc 的 map_ffram 顺序相同的分段命令，并加载真实插件；生产版 Yosys 的加载方式待 B 确认。" if a.real_flow=="split" else "真实模式使用 B 的 patched synth_intel 并加载真实插件。") + "CPU 与峰值内存均取完整综合 Yosys 日志。","",
           "| Case | LUT 基线→优化 | DFF 基线→优化 | IO 基线→优化 | CPU s 基线→优化 | Peak MB 基线→优化 | 改写位 | Structure | PMUX position | EQY | Method | ABC error |",
           "|---|---:|---:|---:|---:|---:|---:|---|---|---|---|---|"]
    for r in rows:
        base=r.get("baseline",{})
        lines.append(f"| {r['case']} | {base.get('lut_count','')}→{r.get('lut','')} | {base.get('dff_count','')}→{r.get('dff','')} | {base.get('io_count','')}→{r.get('io','')} | {base.get('cpu_time','')}→{r.get('cpu_s','')} | {base.get('peak_memory','')}→{r.get('peak_memory_mb','')} | {r.get('changed_bits','')} | {r['structure']} | {r['pass_location']} | {r['eqy']} | {r['eqy_method']} | {r.get('abc_internal_error','')} |")
    save(a.out/"summary.md","\n".join(lines)+"\n")
    return 0 if all_ok and (a.mode=="identity" or benefit=="PASS" or len(rows)<4) else 1


if __name__=="__main__":sys.exit(main())
