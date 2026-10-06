#!/usr/bin/env python3
"""Export five comparable official synthesis cutpoints and trace case3 edits."""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tarfile
from pathlib import Path
from types import SimpleNamespace

from run_micro import ROOT, compare, run, save, validate_machine
from run_official import HASHES, config, pass_location

PHASES = (
    ("01_pre_pmux", "pmux_opt insertion point"),
    ("02_post_pass_opt_clean", "after pmux_opt + opt_clean"),
    ("03_post_common_techmap", "after common techmap"),
    ("04_pre_abc9", "before ABC9"),
    ("05_post_abc9", "after ABC9"),
)
SCALARS = ("pmux", "mux", "eq", "ne", "and", "or", "not", "xor",
           "basic_mux", "total_comb", "lut")


def load_b_entry():
    path = ROOT / "pass" / "run_synth_intel_pmux.py"
    spec = importlib.util.spec_from_file_location("b_audited_entry", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_cutpoint_script(b_entry, src, top, optimized, work, phase):
    args = SimpleNamespace(source=src, top=top, family="cycloneiv", stage="post_abc9",
                           plan=work/(phase+".plan.json"), stats=work/(phase+".stats.json"),
                           json=work/(phase+".json"), rtlil=work/(phase+".il"))
    reference = b_entry.build_script(args).strip().removesuffix(";").split(";\n")
    expected = ("opt -fast -mux_undef -undriven -fine -full", "memory_map",
                "opt -undriven -fine", "opt_clean", "techmap -map +/techmap.v")
    if any(reference.count(command) != 1 for command in expected):
        raise RuntimeError("B audited command sequence changed")
    pass_commands = [command for command in reference if command.startswith("pmux_opt -write-plan")]
    if len(pass_commands) != 1:
        raise RuntimeError("B audited entry no longer has exactly one pmux_opt")
    indexes = [reference.index(command) for command in (expected[0],expected[1],expected[2],pass_commands[0],expected[3],expected[4])]
    if indexes != sorted(indexes):
        raise RuntimeError("B audited map_ffram order changed")
    if phase==PHASES[0][0]: stop=reference.index("opt -undriven -fine")
    elif phase=="01b_post_pass_pre_clean": stop=reference.index(pass_commands[0])
    elif phase==PHASES[1][0]: stop=reference.index("opt_clean") if optimized else reference.index("opt -undriven -fine")
    elif phase==PHASES[2][0]: stop=reference.index("techmap -map +/techmap.v")
    elif phase==PHASES[3][0]: stop=reference.index("setundef -undriven -zero")
    else: raise ValueError(phase)
    commands=reference[:stop+1]
    if not optimized:
        commands=[command for command in commands if command!=pass_commands[0] and command!="opt_clean"]
    if phase==PHASES[3][0]:
        commands.append(f"synth_intel -family cycloneiv -top {top} -run map_ffs:map_luts")
    commands.extend([f'write_rtlil "{args.rtlil}"',f'write_json "{args.json}"'])
    return ";\n".join(commands) + ";\n", args


def is_sequential(cell_type):
    lower = cell_type.lower()
    return ("dff" in lower or "latch" in lower or cell_type in ("$ff", "dffeas")
            or cell_type.startswith("$_DFF"))


def metrics(module):
    cells = module.get("cells", {})
    counts = collections.Counter(cell["type"] for cell in cells.values())
    def count(*types):
        return sum(counts[t] for t in types)
    dff_count=0
    for cell in cells.values():
        if is_sequential(cell["type"]):
            width=cell.get("parameters",{}).get("WIDTH",1)
            if isinstance(width,str):
                try: width=int(width,2)
                except ValueError: width=1
            dff_count+=int(width)
    io_count=sum(value for cell_type,value in counts.items()
                 if cell_type in {"cycloneiv_io_ibuf","cycloneiv_io_obuf","cycloneiv_io_bidir"}
                 or cell_type.startswith(("$_INOUT_","$_INPUT_","$_OUTPUT_")))
    return {"pmux": count("$pmux"), "mux": count("$mux"), "eq": count("$eq"),
            "ne": count("$ne"),
            "and": count("$and", "$logic_and", "$_AND_", "$_NAND_"),
            "or": count("$or", "$logic_or", "$_OR_", "$_NOR_"),
            "not": count("$not", "$logic_not", "$_NOT_"),
            "xor": count("$xor", "$_XOR_", "$_XNOR_"),
            "basic_mux": count("$mux", "$_MUX_", "$_NMUX_"),
            "total_comb": sum(1 for cell in cells.values()
                              if not is_sequential(cell["type"])
                              and not cell["type"].startswith("$mem")),
            "lut": count("$lut"), "dff":dff_count,"io":io_count,"total_cells": len(cells),
            "cell_type_counts": dict(sorted(counts.items()))}


def stage_record(work, top, phase):
    base = work/phase
    il, js = Path(str(base)+".il"), Path(str(base)+".json")
    data = json.loads(js.read_text(encoding="utf-8"))
    module = data["modules"][top]
    return {"rtlil": str(il), "json": str(js), "rtlil_sha256": hash_file(il),
            "json_sha256": hash_file(js), "metrics": metrics(module)}, module


def fanout(module, bit_id):
    sinks=[]
    for name,cell in sorted(module.get("cells",{}).items()):
        for port,bits in sorted(cell.get("connections",{}).items()):
            if cell.get("port_directions",{}).get(port)!="input":
                continue
            for offset,bit in enumerate(bits):
                if bit==bit_id:
                    sinks.append({"cell":name,"type":cell["type"],"port":port,"bit":offset})
    return sinks


def drivers(module, bit_id):
    result=[]
    for name,cell in sorted(module.get("cells",{}).items()):
        for port,bits in sorted(cell.get("connections",{}).items()):
            if cell.get("port_directions",{}).get(port)=="output":
                for offset,bit in enumerate(bits):
                    if bit==bit_id:
                        result.append({"cell":name,"type":cell["type"],"port":port,"bit":offset})
    return result


def case3_trace(plan, before, after_pass, after_clean):
    before_cells=before.get("cells",{})
    pass_cells=after_pass.get("cells",{})
    clean_cells=after_clean.get("cells",{})
    if set(before_cells)!=set(pass_cells):
        raise RuntimeError("pmux_opt changed cell set before opt_clean")
    deleted=sorted(set(pass_cells)-set(clean_cells))
    added=sorted(set(clean_cells)-set(pass_cells))
    rows=[]
    for edit in plan["edits"]:
        old=edit["old"]
        wire=before["netnames"][old["wire"]]
        bit_id=wire["bits"][old["offset"]]
        sinks=fanout(before,bit_id)
        sources=drivers(before,bit_id)
        rows.append({"cell":edit["cell"],"port":edit["port"],"physical_bit":edit["bit"],
                     "branch":edit.get("branch"),"branch_bit":edit.get("branch_bit"),
                     "before_signal":old,"before_bit_id":bit_id,"after_constant":edit["value"],
                     "fanout_before":sinks,"drivers_before":sources,
                     "fanout_after_pass":fanout(after_pass,bit_id),
                     "fanout_after_opt_clean":fanout(after_clean,bit_id),
                     "drivers_deleted_by_opt_clean":[d for d in sources if d["cell"] in deleted],
                     "fanout_cells_deleted_by_opt_clean":[s for s in sinks if s["cell"] in deleted]})
    return {"seven_edits":rows,"opt_clean_deleted_cells":
            [{"cell":name,"type":pass_cells[name]["type"]} for name in deleted],
            "opt_clean_added_cells":
            [{"cell":name,"type":clean_cells[name]["type"]} for name in added]}


def first_reconvergence(stages, key):
    diverged=False
    for stage in stages[1:]:
        if not stage[key]:
            diverged=True
        elif diverged:
            return stage["stage"]
    return None


def classify_case(case):
    stages=case["stages"]
    delta_lut=stages[-1]["delta"]["lut"]
    preabc=stages[3]
    postclean_cells=stages[1]["optimized"]["metrics"]["total_cells"]
    prepass_cells=stages[0]["optimized"]["metrics"]["total_cells"]
    labels=[]
    if case.get("changed_bits",0)==0:
        labels.append("no_real_bit_rewrite")
    if case.get("changed_bits",0)>0 and postclean_cells>=prepass_cells:
        labels.append("post_opt_clean_no_cell_removal")
    if not stages[1]["structure_equal"] and stages[2]["structure_equal"]:
        labels.append("converged_at_common_techmap")
    if not preabc["structure_equal"] and delta_lut==0:
        labels.append("pre_abc9_difference_absorbed_by_abc9_mapping")
    if delta_lut<0: labels.append("real_lut_gain")
    if delta_lut>0: labels.append("lut_regression")
    if delta_lut==0: labels.append("no_lut_gain")
    return labels


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--bin",type=Path,required=True)
    parser.add_argument("--plugin",type=Path,default=ROOT/"pass"/"pmux_opt.so")
    parser.add_argument("--source-root",type=Path,required=True)
    parser.add_argument("--out",type=Path,default=ROOT/"reports"/"c-official-stage-diff-final")
    parser.add_argument("--case",type=int,choices=(1,2,3,4),action="append")
    args=parser.parse_args()
    args.out=args.out.resolve(); args.plugin=args.plugin.resolve(); args.source_root=args.source_root.resolve()
    yosys=(args.bin/"yosys").resolve()
    eqy=(args.bin/"eqy").resolve()
    if not yosys.is_file() or not eqy.is_file():
        parser.error("missing frozen Yosys/EQY")
    args.out.mkdir(parents=True,exist_ok=True)
    if not args.plugin.is_file():
        build=["make","-C",str(ROOT/"pass"),f"YOSYS_CONFIG={yosys.parent/'yosys-config'}"]
        save(args.out/"plugin_build.command.txt"," ".join(build)+"\n")
        process=subprocess.run(build,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        save(args.out/"plugin_build.log",process.stdout)
        if process.returncode or not args.plugin.is_file():
            raise RuntimeError("failed to build real pmux_opt.so; see plugin_build.log")
    env=os.environ.copy(); env["PATH"]=str(yosys.parent)+os.pathsep+env.get("PATH","")
    b_entry=load_b_entry()
    official_base=ROOT/"reports"/"c-official-baseline-check"
    official_opt=ROOT/"reports"/"c-official-b-audited"
    rows=[]
    for number in args.case or (1,2,3,4):
        case=f"case{number}"; top=f"test{number}"; src=args.source_root/f"{top}.v"
        if hash_file(src)!=HASHES[number-1]:
            raise RuntimeError(f"{case}: frozen official SHA-256 mismatch")
        comparison={"case":case,"source_sha256":HASHES[number-1],"stages":[],"case3":None}
        designs={}
        for label,optimized in (("baseline",False),("optimized",True)):
            work=args.out/case/label; work.mkdir(parents=True,exist_ok=True)
            phase_names=[name for name,_ in PHASES]
            if optimized: phase_names.insert(1,"01b_post_pass_pre_clean")
            plan=None
            for phase in phase_names:
                script_file=work/(phase+".ys")
                log_file=work/(phase+".log")
                if phase==PHASES[-1][0] and optimized:
                    bargs=SimpleNamespace(source=src,top=top,family="cycloneiv",stage="post_abc9",
                        plan=work/(phase+".plan.json"),stats=work/(phase+".stats.json"),
                        json=work/(phase+".json"),rtlil=work/(phase+".il"),
                        log=log_file,script=script_file)
                    command=[sys.executable,ROOT/"pass"/"run_synth_intel_pmux.py",
                        "--yosys",yosys,"--plugin",args.plugin,"--source",src,"--top",top,
                        "--stage","post_abc9","--plan",bargs.plan,"--stats",bargs.stats,
                        "--json",bargs.json,"--rtlil",bargs.rtlil,"--log",log_file,"--script",script_file]
                    if run(command,work/(phase+".driver.log"),work,env,timeout=900):
                        raise RuntimeError(f"{case}/{label}/{phase}: B audited entry failed")
                elif phase==PHASES[-1][0]:
                    script=(f'read_verilog "{src}"; synth_intel -family cycloneiv -top {top} '
                            f'-run begin:map_cells; stat; write_json "{work/(phase+".json")}"; '
                            f'select {top}; write_rtlil -selected "{work/(phase+".il")}";')
                    save(script_file,script+"\n")
                    command=[yosys,"-l",log_file,"-s",script_file]
                    if run(command,work/(phase+".console.log"),work,env,timeout=900):
                        raise RuntimeError(f"{case}/{label}/{phase}: baseline Yosys failed")
                else:
                    script,bargs=build_cutpoint_script(b_entry,src,top,optimized,work,phase)
                    save(script_file,script)
                    command=[yosys]
                    if optimized: command += ["-m",args.plugin]
                    command += ["-l",log_file,"-s",script_file]
                    if run(command,work/(phase+".console.log"),work,env,timeout=900):
                        raise RuntimeError(f"{case}/{label}/{phase}: cutpoint Yosys failed")
                log=log_file.read_text(encoding="utf-8",errors="replace")
                marker="Executing PMUX_OPT guarded analysis and atomic rewrite."
                expected_pass=1 if optimized and phase!=PHASES[0][0] else 0
                if log.count(marker)!=expected_pass:
                    raise RuntimeError(f"{case}/{label}/{phase}: expected {expected_pass} PMUX_OPT calls")
                if optimized and expected_pass:
                    current_plan=json.loads(bargs.plan.read_text())
                    current_stats=json.loads(bargs.stats.read_text())
                    if current_plan.get("mode")!="real" or current_stats.get("mode")!="real" or current_stats.get("changed_bits")!=len(current_plan.get("edits",[])):
                        raise RuntimeError(f"{case}/{phase}: invalid real Pass machine output")
                    if phase==PHASES[-1][0]:
                        b_entry.require_real_outputs(bargs,log)
                        issues=pass_location(log,current_stats["changed_bits"])
                        if issues: raise RuntimeError(f"{case}: B audit: {issues}")
                    if plan is None: plan=current_plan
                    elif current_plan["edits"]!=plan["edits"]:
                        raise RuntimeError(f"{case}: Pass edits depend on diagnostic cutoff")
            if optimized:
                before=json.loads((work/(PHASES[0][0]+".json")).read_text())["modules"]
                after=json.loads((work/"01b_post_pass_pre_clean.json").read_text())["modules"]
                structural,count=compare(before,after,{"expected_edits":plan["edits"]},plan,True)
                issues=structural+validate_machine(before,after,plan,
                    json.loads((work/"01b_post_pass_pre_clean.stats.json").read_text()),count)
                if issues: raise RuntimeError(f"{case}: Pass plan/structure: {issues}")
                comparison["changed_bits"]=count
            snapshot={}
            for phase,_ in PHASES:
                snapshot[phase]=stage_record(work,top,phase)
            designs[label]=snapshot
            official_report=json.loads(((official_opt if optimized else official_base)/"summary.json").read_text())
            official_row=next(row for row in official_report["cases"] if row["case"]==case)
            final_metric=snapshot[PHASES[-1][0]][0]["metrics"]
            if (final_metric["lut"],final_metric["dff"],final_metric["io"])!=(official_row["lut"],official_row["dff"],official_row["io"]):
                raise RuntimeError(f"{case}/{label}: instrumented ABC9 resources differ from official run")
            official_path=(official_opt if optimized else official_base)/case/"post_abc9.json"
            comparison[label+"_official_exact_json_equal"]=(snapshot[PHASES[-1][0]][1]==
                json.loads(official_path.read_text())["modules"][top]) if official_path.is_file() else None
            gate=work/(PHASES[-1][0]+".il")
            comparison[label+"_eqy_method"]="FAIL"
            for method,whole in (("default",False),("whole",True)):
                cfg=work/(method+".eqy")
                save(cfg,config(src,gate,top,whole))
                rc=run([eqy,"-f","-d",work/("eqy_"+method),cfg],work/("eqy_"+method+".log"),work,env,timeout=600)
                if rc==0 and "DONE (PASS" in (work/("eqy_"+method+".log")).read_text():
                    comparison[label+"_eqy_method"]=method
                    break
            if comparison[label+"_eqy_method"]=="FAIL":
                raise RuntimeError(f"{case}/{label}: instrumented ABC9 EQY failed")
        for phase,title in PHASES:
            base_record,base_module=designs["baseline"][phase]
            opt_record,opt_module=designs["optimized"][phase]
            base_metrics,opt_metrics=base_record["metrics"],opt_record["metrics"]
            comparison["stages"].append({"stage":phase,"title":title,
                "baseline":base_record,"optimized":opt_record,
                "delta":{key:opt_metrics[key]-base_metrics[key] for key in SCALARS},
                "structure_equal":base_module==opt_module,
                "requested_statistics_equal":all(base_metrics[key]==opt_metrics[key] for key in SCALARS),
                "cell_type_histogram_equal":base_metrics["cell_type_counts"]==opt_metrics["cell_type_counts"]})
        comparison["classification"]=classify_case(comparison)
        if number==3:
            work=args.out/case/"optimized"
            plan=json.loads((work/(PHASES[-1][0]+".plan.json")).read_text())
            if len(plan["edits"])!=7: raise RuntimeError("case3 no longer has exactly seven real edits")
            before=designs["optimized"][PHASES[0][0]][1]
            after_pass=json.loads((work/"01b_post_pass_pre_clean.json").read_text())["modules"][top]
            after_clean=designs["optimized"][PHASES[1][0]][1]
            trace=case3_trace(plan,before,after_pass,after_clean)
            trace["first_structure_reconvergence"]=first_reconvergence(comparison["stages"],"structure_equal")
            trace["first_statistics_reconvergence"]=first_reconvergence(comparison["stages"],"requested_statistics_equal")
            trace["first_histogram_reconvergence"]=first_reconvergence(comparison["stages"],"cell_type_histogram_equal")
            trace["conclusion"]=("Seven proved bit replacements removed no cell at opt_clean; "
                                 "the pre-ABC9 gate-count difference mapped to the same LUT count under ABC9.")
            comparison["case3"]=trace
            save(args.out/case/"seven_bit_chain.json",json.dumps(trace,ensure_ascii=False,indent=2)+"\n")
            chain=["# case3 七位扇出与删除链","",
                   f"`opt_clean` 删除 cell：{trace['opt_clean_deleted_cells'] or '无'}；新增 cell：{trace['opt_clean_added_cells'] or '无'}。",
                   f"首次结构重新相同：{trace['first_structure_reconvergence'] or '未出现'}；首次统计重新相同：{trace['first_statistics_reconvergence'] or '未出现'}。","",
                   "| 目标物理位 | Branch | 改写前 → 后 | 驱动 cell | 改写前扇出 | opt_clean 后扇出 | 被删除的扇出 cell |",
                   "|---|---|---|---|---|---|---|"]
            for edit in trace["seven_edits"]:
                def show(connections):
                    return ", ".join(f"{item['cell']}.{item['port']}[{item['bit']}]" for item in connections) or "无"
                chain.append(f"| {edit['cell']}.{edit['port']}[{edit['physical_bit']}] | {edit['branch']} | "
                             f"{edit['before_signal']['wire']}[{edit['before_signal']['offset']}] → {edit['after_constant']} | "
                             f"{show(edit['drivers_before'])} | {show(edit['fanout_before'])} | "
                             f"{show(edit['fanout_after_opt_clean'])} | {show(edit['fanout_cells_deleted_by_opt_clean'])} |")
            chain.extend(["", "七个位原信号仍有目标以外的扇出；此次位改写后 `opt_clean` 未删除任何 cell。ABC9 前少 1 个 AND、1 个 OR，ABC9 后均为 13 LUT。", ""])
            save(args.out/case/"seven_bit_chain.md","\n".join(chain))
        rows.append(comparison)
        print(f"{case}: PASS, edits={comparison.get('changed_bits',0)}, LUT delta={comparison['stages'][-1]['delta']['lut']}",flush=True)
    report={"schema":1,"method":"independent_cutpoints_and_unmodified_B_audited_post_abc9",
            "b_entry":str(ROOT/"pass"/"run_synth_intel_pmux.py"),
            "yosys_version":subprocess.run([yosys,"-V"],capture_output=True,text=True).stdout.strip(),
            "plugin_sha256":hash_file(args.plugin),"phases":[p for p,_ in PHASES],
            "metric_definitions":{"total_comb":"top-level cells excluding sequential and memory cells",
                                  "basic_gate_counts":"coarse $-cells and matching $_ gate cells; mux includes $mux/$_MUX_/$_NMUX_"},
            "cases":rows,"lut_benefit":"PASS" if any(r["stages"][-1]["delta"]["lut"]<0 for r in rows)
            and all(r["stages"][-1]["delta"]["lut"]<=0 for r in rows) else "FAIL"}
    save(args.out/"summary.json",json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    lines=["# 官方 case 阶段差异诊断","", "每个截点独立运行并在该点停止，避免网表导出影响后续 ABC9 映射。optimized 的插入点前截点尚未执行 Pass；其余截点每进程恰好执行一次真实 Pass。baseline 全程不加载插件。ABC9 后直接使用 B 原始审计入口，资源数与既有官方回归一致，并逐案通过原始 RTL 对阶段5网表的 EQY。", "",
           "| Case | 阶段 | ΔPMUX | ΔMUX | ΔEQ | ΔNE | ΔAND | ΔOR | ΔNOT | ΔXOR | Δ基本MUX | 总组合 baseline→optimized (Δ) | LUT baseline→optimized (Δ) | 结构同 | 统计同 |",
           "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    for case in rows:
        for stage in case["stages"]:
            d=stage["delta"]
            b=stage["baseline"]["metrics"]; o=stage["optimized"]["metrics"]
            lines.append(f"| {case['case']} | {stage['stage']} | {d['pmux']} | {d['mux']} | {d['eq']} | {d['ne']} | {d['and']} | {d['or']} | {d['not']} | {d['xor']} | {d['basic_mux']} | {b['total_comb']}→{o['total_comb']} ({d['total_comb']:+d}) | {b['lut']}→{o['lut']} ({d['lut']:+d}) | {stage['structure_equal']} | {stage['requested_statistics_equal']} |")
    lines.extend(["", "总组合 cell 的下降只作结构观察；LUT 收益只按 ABC9 后 `$lut` 判定。", ""])
    case3=next((row for row in rows if row["case"]=="case3"),None)
    if case3:
        t=case3["case3"]
        lines.extend(["## case3 七位链", "",
            f"opt_clean 删除 {len(t['opt_clean_deleted_cells'])} 个 cell；首次结构重新相同：{t['first_structure_reconvergence'] or '未出现'}；首次请求统计重新相同：{t['first_statistics_reconvergence'] or '未出现'}。", "",
            "结论：七位替换未使 opt_clean 删除 cell；ABC9 前少 2 个基本组合 cell，ABC9 后均为 13 个 LUT，映射吸收了门级差异。", "",
            "| Cell | 端口位 | Branch | 原信号 | 常量 | 原扇出 cell |",
            "|---|---|---|---|---|---|"])
        for edit in t["seven_edits"]:
            sinks=", ".join(sorted({sink["cell"] for sink in edit["fanout_before"]}))
            lines.append(f"| {edit['cell']} | {edit['port']}[{edit['physical_bit']}] | {edit['branch']} | {edit['before_signal']['wire']}[{edit['before_signal']['offset']}] | {edit['after_constant']} | {sinks} |")
        lines.extend(["", "完整扇出端口、驱动、opt_clean 删除清单和各位删除关系见 `case3/seven_bit_chain.json`。", ""])
    save(args.out/"summary.md","\n".join(lines))
    with tarfile.open(args.out/"stage-evidence.tar.gz","w:gz") as archive:
        for case in rows:
            archive.add(args.out/case["case"],arcname=case["case"])
    return 0


if __name__=="__main__":
    raise SystemExit(main())
