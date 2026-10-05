#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BASE_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

for index in 1 2 3 4; do
    case_dir="${BASE_DIR}/case${index}"
    [[ "$(<"${case_dir}/logs/full.returncode")" == 0 ]]
    [[ "$(<"${case_dir}/logs/post_abc9.returncode")" == 0 ]]
    grep -Fq 'cell $lut' "${case_dir}/netlists/post_abc9.il"
    grep -q '^PASS' "${case_dir}/eqy/status.txt"
    grep -q 'DONE (PASS' "${case_dir}/eqy/final.log"
done

if grep -EHi 'pmux_local|pmux_opt|elinx|plugin -i|plugin -a' "${BASE_DIR}"/case*/logs/commands.log; then
    echo "forbidden custom optimization/plugin reference found" >&2
    exit 1
fi

python3 - "${BASE_DIR}" <<'PY'
import csv, json, sys
from pathlib import Path

base = Path(sys.argv[1])
rows = list(csv.DictReader((base / "results" / "baseline.csv").open(encoding="utf-8-sig")))
assert len(rows) == 4, f"expected 4 CSV rows, got {len(rows)}"
for row in rows:
    case = row["case"]
    metrics = json.loads((base / case / "netlists" / "post_abc9_metrics.json").read_text())
    assert row["synthesis_status"] == "PASS"
    assert row["eqy_status"].startswith("PASS")
    assert row["lut_ilang_status"] == "PASS"
    for field in ("lut_count", "dff_count", "io_count", "total_cells"):
        assert int(row[field]) == int(metrics[field]), (case, field, row[field], metrics[field])
    assert int(row["lut_count"]) > 0

stage_rows = list(csv.DictReader((base / "results" / "stage_counts.csv").open(encoding="utf-8-sig")))
assert len(stage_rows) == 24, f"expected 24 stage rows, got {len(stage_rows)}"
assert {(r["case"], r["stage"]) for r in stage_rows} == {
    (f"case{i}", stage) for i in range(1, 5)
    for stage in ("proc", "basic_opt", "before_common_techmap", "after_common_techmap", "pre_abc9", "post_abc9")
}
print("CSV/log/netlist consistency: PASS")
PY

echo "all four official baselines verified: PASS"
