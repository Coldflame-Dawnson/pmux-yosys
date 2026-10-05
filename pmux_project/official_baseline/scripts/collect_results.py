#!/usr/bin/env python3
import csv
import json
import re
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
ROWS = []
STAGE_ROWS = []


def yosys_runtime(log_path):
    text = log_path.read_text(encoding="utf-8", errors="replace")
    matches = re.findall(
        r"End of script\..*?user:\s*([0-9.]+)s, system:\s*([0-9.]+)s, MEM:\s*([0-9.]+) MB peak",
        text,
    )
    if not matches:
        raise RuntimeError(f"cannot parse Yosys runtime from {log_path}")
    user, system, memory = map(float, matches[-1])
    warning_matches = re.findall(r"Warnings:\s+\d+ unique messages,\s+(\d+) total", text)
    warnings = int(warning_matches[-1]) if warning_matches else 0
    return user + system, memory, warnings


version = (BASE / "environment" / "yosys_version.txt").read_text(encoding="utf-8").strip()
for index in range(1, 5):
    case = f"case{index}"
    top = f"test{index}"
    source = BASE.parent / "pmux_case" / "competition_case" / f"test{index}.v"
    case_dir = BASE / case
    metrics = json.loads((case_dir / "netlists" / "post_abc9_metrics.json").read_text(encoding="utf-8"))
    cpu, memory, warnings = yosys_runtime(case_dir / "logs" / "full.log")
    full_rc = int((case_dir / "logs" / "full.returncode").read_text().strip())
    post_rc = int((case_dir / "logs" / "post_abc9.returncode").read_text().strip())
    eqy_status = (case_dir / "eqy" / "status.txt").read_text(encoding="utf-8").strip()
    synthesis_status = "PASS" if full_rc == 0 and post_rc == 0 else "FAIL"
    notes = [f"Yosys warnings={warnings} (synth_intel experimental warning expected)"]
    if metrics["io_count"] == 0:
        notes.append("no IO cells because unmodified synth_intel was run without -iopads")
    notes.append("dffeas formal-model subset check=" + metrics["dffeas_model_check"])
    ROWS.append({
        "case": case,
        "top_module": top,
        "source_files": str(source),
        "yosys_version": version,
        "lut_count": metrics["lut_count"],
        "dff_count": metrics["dff_count"],
        "io_count": metrics["io_count"],
        "total_cells": metrics["total_cells"],
        "cpu_time": f"{cpu:.2f} s",
        "peak_memory": f"{memory:.2f} MB",
        "synthesis_status": synthesis_status,
        "eqy_status": eqy_status,
        "warnings": warnings,
        "lut_ilang_status": "PASS" if metrics["lut_count"] > 0 else "FAIL",
        "full_return_code": full_rc,
        "post_abc9_return_code": post_rc,
        "notes": "; ".join(notes),
    })
    for stage, metrics_name in [
        ("proc", "stage_proc_metrics.json"),
        ("basic_opt", "stage_opt_metrics.json"),
        ("before_common_techmap", "stage_before_common_techmap_metrics.json"),
        ("after_common_techmap", "stage_after_common_techmap_metrics.json"),
        ("pre_abc9", "stage_pre_abc9_metrics.json"),
        ("post_abc9", "post_abc9_metrics.json"),
    ]:
        sm = json.loads((case_dir / "netlists" / metrics_name).read_text(encoding="utf-8"))
        STAGE_ROWS.append({
            "case": case,
            "top_module": top,
            "stage": stage,
            "pmux_count": sm["pmux_count"],
            "mux_count": sm["mux_count"],
            "eq_count": sm["eq_count"],
            "ne_count": sm["ne_count"],
            "and_count": sm["and_count"],
            "or_count": sm["or_count"],
            "xor_count": sm["xor_count"],
            "not_count": sm["not_count"],
            "lut_count": sm["lut_count"],
            "dff_count": sm["dff_count"],
            "total_cells": sm["total_cells"],
            "major_comb_cells": ";".join(f"{k}={v}" for k, v in sm["major_comb_cells"].items()),
        })

results = BASE / "results"
results.mkdir(exist_ok=True)
fields = list(ROWS[0])
with (results / "baseline.csv").open("w", newline="", encoding="utf-8-sig") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(ROWS)

stage_fields = list(STAGE_ROWS[0])
with (results / "stage_counts.csv").open("w", newline="", encoding="utf-8-sig") as f:
    writer = csv.DictWriter(f, fieldnames=stage_fields)
    writer.writeheader()
    writer.writerows(STAGE_ROWS)

md_fields = ["case", "top_module", "lut_count", "dff_count", "io_count", "total_cells", "cpu_time", "peak_memory", "synthesis_status", "eqy_status"]
lines = [
    "# Official PMUX baseline results",
    "",
    "| " + " | ".join(md_fields) + " |",
    "| " + " | ".join(["---"] * len(md_fields)) + " |",
]
for row in ROWS:
    lines.append("| " + " | ".join(str(row[field]).replace("|", "\\|") for field in md_fields) + " |")
lines.append("")
(results / "baseline.md").write_text("\n".join(lines), encoding="utf-8")
