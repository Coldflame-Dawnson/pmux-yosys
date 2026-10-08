#!/usr/bin/env python3
"""C's isolated official baseline/default/custom ABC matrix and generic selection."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(argv, log, env=None):
    log.parent.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    with log.open("w") as stream:
        result = subprocess.run(argv, stdout=stream, stderr=subprocess.STDOUT,
                                text=True, env=env, timeout=900)
    return result.returncode, time.perf_counter() - start

def netlist_stats(path, top):
    module = json.loads(path.read_text())["modules"][top]
    types = {}
    for cell in module.get("cells", {}).values():
        types[cell["type"]] = types.get(cell["type"], 0) + 1
    io = {"input": 0, "output": 0, "inout": 0}
    for port in module["ports"].values():
        io[port["direction"]] += len(port["bits"])
    return {"cell_types": types, "total_cells": sum(types.values()), "io_bits": io,
            "lut": types.get("$lut", 0), "dff": types.get("dffeas", 0)}

def log_times(path):
    data = path.read_text(errors="replace")
    end = re.search(r"End of script\. Logfile hash:.*?time: ([0-9.]+)s, user: ([0-9.]+)s, system: ([0-9.]+)s, MEM: ([0-9.]+) MB peak", data)
    abc = re.findall(r"ABC: elapse: ([0-9.]+) seconds, total: ([0-9.]+) seconds", data)
    return {"yosys_wall_s": float(end.group(1)) if end else None,
            "cpu_user_s": float(end.group(2)) if end else None,
            "cpu_system_s": float(end.group(3)) if end else None,
            "peak_mem_mb": float(end.group(4)) if end else None,
            "abc_elapsed_s": float(abc[-1][0]) if abc else None,
            "check_zero_problems": "Found and reported 0 problems." in data}

def eqy_text(gold, gate, top, formal):
    return f"""[gold]
read_verilog -formal {gold}

[gate]
read_rtlil {gate}
read_verilog -formal -overwrite {formal}

[script]
hierarchy -check -top {top}
prep
memory_map
async2sync

[partition {top}]
merge *

[strategy sat]
use sat
depth 20

[strategy pdr]
use sby
engine abc pdr
timeout 300
"""

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--bin", type=Path, required=True)
    p.add_argument("--official", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    out, bin_dir, official = args.out.resolve(), args.bin.resolve(), args.official.resolve()
    out.mkdir(parents=True, exist_ok=True)
    runner = PROJECT / "pass/run_synth_intel_pmux.py"
    plugin = PROJECT / "pass/pmux_opt.so"
    abc_script = PROJECT / "experiments/case34_v02b/evidence/generated/abc.script"
    formal = PROJECT / "official_baseline/environment/dffeas_formal.v"
    env = os.environ.copy()
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    summary = {"schema": 1, "producer": "c_v02_official_matrix.py", "cases": {}}
    for n in range(1, 5):
        top = f"test{n}"
        gold = official / f"{top}.v"
        rec = {"gold_sha256": sha(gold), "variants": {}}
        for variant in ("baseline", "default", "custom"):
            d = out / top / variant
            d.mkdir(parents=True, exist_ok=True)
            argv = [sys.executable, str(runner), "--yosys", str(bin_dir/"yosys"),
                    "--plugin", str(plugin), "--source", str(gold), "--top", top,
                    "--stage", "post_abc9", "--plan", str(d/"plan.json"),
                    "--stats", str(d/"stats.json"), "--json", str(d/"post_abc9.json"),
                    "--rtlil", str(d/"post_abc9.il"), "--log", str(d/"yosys.log"),
                    "--script", str(d/"run.ys")]
            if variant != "baseline":
                argv.append("--v02-case34")
            if variant == "custom":
                argv += ["--abc-script", str(abc_script)]
            (d/"driver.command.json").write_text(json.dumps(argv, indent=2)+"\n")
            rc, wall = run(argv, d/"driver.log")
            if rc:
                raise RuntimeError(f"{top}/{variant} synthesis rc={rc}")
            plan = json.loads((d/"plan.json").read_text())
            stats = json.loads((d/"stats.json").read_text())
            resource = netlist_stats(d/"post_abc9.json", top)
            timing = log_times(d/"yosys.log")
            if not timing["check_zero_problems"]:
                raise RuntimeError(f"{top}/{variant} Yosys check failed")
            eqd = d/"gate_eqy"
            eqd.mkdir(exist_ok=True)
            config = eqd/"verify.eqy"
            config.write_text(eqy_text(gold,d/"post_abc9.il",top,formal))
            eqargv = [str(bin_dir/"eqy"),"-f","-d",str(eqd/"work"),str(config)]
            (eqd/"command.json").write_text(json.dumps(eqargv,indent=2)+"\n")
            erc, ewall = run(eqargv,eqd/"eqy.log",env)
            eqpass = erc==0 and (eqd/"work/PASS").is_file() and "DONE (PASS" in (eqd/"eqy.log").read_text()
            if not eqpass:
                raise RuntimeError(f"{top}/{variant} EQY failed")
            rec["variants"][variant] = {
                "resources":resource,"timing":timing,"driver_wall_s":wall,
                "eqy_wall_s":ewall,"eqy_pass":eqpass,"gate_sha256":sha(d/"post_abc9.il"),
                "plan":plan,"stats":stats
            }
            print(top,variant,resource["lut"],resource["dff"],"EQY PASS",flush=True)
        base = rec["variants"]["baseline"]
        eligible = []
        for variant in ("default","custom"):
            item = rec["variants"][variant]
            structural = item["plan"].get("structural_edits", [])
            if (structural and item["eqy_pass"]
                    and item["resources"]["lut"] <= base["resources"]["lut"]
                    and item["resources"]["dff"] <= base["resources"]["dff"]
                    and item["resources"]["io_bits"] == base["resources"]["io_bits"]):
                eligible.append(variant)
        selected = min(eligible,
            key=lambda v:(rec["variants"][v]["resources"]["lut"],
                          rec["variants"][v]["resources"]["dff"],
                          rec["variants"][v]["resources"]["total_cells"],v)
            ) if eligible else "baseline"
        rec["selected"] = selected
        rec["selection_reason"] = "eligible_structural_candidate_min_resource_eqy" if eligible else "no_eligible_structural_candidate"
        summary["cases"][top] = rec
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("SELECTED", {k:v["selected"] for k,v in summary["cases"].items()})
    return 0

if __name__=="__main__":
    raise SystemExit(main())