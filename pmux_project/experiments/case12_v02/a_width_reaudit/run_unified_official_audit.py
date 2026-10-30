#!/usr/bin/env python3
"""Independently regenerate and verify all four official cases on the unified path."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

EXPECTED_HASHES = {
    1: "2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13",
    2: "e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146",
    3: "1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9",
    4: "c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7",
}
EXPECTED_COUNTS = {
    1: {"$lut": 182, "dffeas": 222, "cycloneiv_lcell_comb": 0},
    2: {"$lut": 0, "dffeas": 0, "cycloneiv_lcell_comb": 15},
    3: {"$lut": 11, "dffeas": 12, "cycloneiv_lcell_comb": 0},
    4: {"$lut": 27, "dffeas": 10, "cycloneiv_lcell_comb": 0},
}


def run(argv, log, env, timeout=900):
    started = time.perf_counter()
    completed = subprocess.run(argv, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=env, timeout=timeout)
    log.write_text(completed.stdout, encoding="utf-8")
    return completed.returncode, time.perf_counter() - started


def counts(path, top):
    document = json.loads(path.read_text(encoding="utf-8"))
    result = {}
    for cell in document["modules"][top].get("cells", {}).values():
        kind = cell["type"]
        result[kind] = result.get(kind, 0) + 1
    return dict(sorted(result.items()))


def eqy_text(source, gate, top, dffeas, lcell_model, needs_lcell):
    gate_reads = ""
    if needs_lcell:
        gate_reads += f"read_verilog -formal {lcell_model}\n"
    gate_reads += f"read_rtlil {gate}\nread_verilog -formal -overwrite {dffeas}\n"
    return f"""[gold]
read_verilog -formal {source}

[gate]
{gate_reads}
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--tool-bin", type=Path, required=True)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    repo, tool_bin, official, out = map(Path.resolve,
                                         (args.repo, args.tool_bin, args.official, args.out))
    out.mkdir(parents=True, exist_ok=True)
    yosys, eqy = tool_bin / "yosys", tool_bin / "eqy"
    plugin = repo / "pmux_project/pass/pmux_opt.so"
    runner = repo / "pmux_project/experiments/case12_v02/a_width_reaudit/run_synth_intel_case12_audit.py"
    dffeas = repo / "pmux_project/official_baseline/environment/dffeas_formal.v"
    lcell_model = repo / "pmux_project/experiments/case12_v02/a_width_reaudit/cycloneiv_lcell_comb_formal.v"
    env = os.environ.copy()
    env["PATH"] = str(tool_bin) + os.pathsep + env.get("PATH", "")
    rows = []
    for index in range(1, 5):
        top = f"test{index}"
        source = official / f"{top}.v"
        work = out / f"case{index}"
        work.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        row = {"case": index, "source_sha256": digest, "issues": []}
        if digest != EXPECTED_HASHES[index]:
            row["issues"].append("official source hash mismatch")
            rows.append(row)
            continue
        for stage in ("post_abc9", "full"):
            argv = [sys.executable, str(runner), "--yosys", str(yosys),
                    "--plugin", str(plugin), "--source", str(source), "--top", top,
                    "--stage", stage, "--plan", str(work / f"{stage}.plan.json"),
                    "--stats", str(work / f"{stage}.stats.json"),
                    "--json", str(work / f"{stage}.json"),
                    "--log", str(work / f"{stage}.yosys.log"),
                    "--script", str(work / f"{stage}.ys")]
            if stage == "post_abc9":
                argv += ["--rtlil", str(work / "post_abc9.il")]
            rc, elapsed = run(argv, work / f"{stage}.driver.log", env)
            row[f"{stage}_rc"] = rc
            row[f"{stage}_wall_seconds"] = elapsed
            if rc:
                row["issues"].append(f"{stage} runner rc={rc}")
        if row["issues"]:
            rows.append(row)
            continue
        observed = counts(work / "post_abc9.json", top)
        row["cell_counts"] = observed
        row["count_check"] = all(observed.get(kind, 0) == value
                                  for kind, value in EXPECTED_COUNTS[index].items())
        if not row["count_check"]:
            row["issues"].append("post-ABC9/vendor count mismatch")
        full_log = (work / "full.yosys.log").read_text(encoding="utf-8", errors="replace")
        timing = re.findall(r"End of script\..*?user:\s*([0-9.]+)s, system:\s*([0-9.]+)s, MEM:\s*([0-9.]+) MB peak", full_log, re.S)
        if timing:
            user, system, memory = map(float, timing[-1])
            row["cpu_seconds"] = user + system
            row["peak_memory_mb"] = memory
        row["plan"] = json.loads((work / "post_abc9.plan.json").read_text(encoding="utf-8"))
        row["stats"] = json.loads((work / "post_abc9.stats.json").read_text(encoding="utf-8"))
        config = work / "whole.eqy"
        config.write_text(eqy_text(source, work / "post_abc9.il", top, dffeas,
                                   lcell_model, index == 2), encoding="utf-8")
        rc, elapsed = run([str(eqy), "-f", "-d", str(work / "eqy_work"), str(config)],
                          work / "whole.eqy.log", env)
        row["eqy_rc"] = rc
        row["eqy_wall_seconds"] = elapsed
        row["eqy_pass"] = rc == 0 and (work / "eqy_work/PASS").is_file()
        if not row["eqy_pass"]:
            row["issues"].append("whole-design EQY failed")
        row["result"] = "PASS" if not row["issues"] else "FAIL"
        rows.append(row)
        print(f"case{index}: {row['result']} counts={observed} EQY={row['eqy_pass']}", flush=True)
    summary = {"schema": 1, "mode": "explicit_case12_plus_default_case34",
               "expected_counts": EXPECTED_COUNTS, "rows": rows,
               "result": "PASS" if len(rows) == 4 and all(r.get("result") == "PASS" for r in rows) else "FAIL"}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if summary["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
