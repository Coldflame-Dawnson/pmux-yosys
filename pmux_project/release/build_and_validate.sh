#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 TOOL_ROOT OFFICIAL_CASE_DIR [OUTPUT_DIR]" >&2
    exit 2
fi

TOOL_ROOT=$(realpath "$1")
OFFICIAL_DIR=$(realpath "$2")
ROOT=$(git rev-parse --show-toplevel)
OUT=${3:-"$ROOT/release-validation"}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")

EXPECTED_G3=12ac8b86cfd82d1439ae47d4b7b198b26f0e545a
EXPECTED_INPUTS=(
  2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13
  e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146
  1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9
  c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7
)

git merge-base --is-ancestor "$EXPECTED_G3" HEAD || {
    echo "current tree does not descend from verified G3 $EXPECTED_G3" >&2
    exit 1
}

for tool in yosys yosys-config eqy yosys-abc; do
    test -x "$TOOL_ROOT/bin/$tool" || {
        echo "missing executable: $TOOL_ROOT/bin/$tool" >&2
        exit 1
    }
done

for index in 1 2 3 4; do
    file="$OFFICIAL_DIR/test${index}.v"
    test -f "$file" || { echo "missing official input: $file" >&2; exit 1; }
    actual=$(sha256sum "$file" | awk '{print $1}')
    expected=${EXPECTED_INPUTS[$((index - 1))]}
    test "$actual" = "$expected" || {
        echo "official input hash mismatch: test${index}.v" >&2
        exit 1
    }
done

PLUGIN="$OUT/pmux_opt.so"
"$TOOL_ROOT/bin/yosys-config" --build "$PLUGIN" \
    "$ROOT/pmux_project/pass/pmux_opt.cc" \
    "$ROOT/pmux_project/pass/rewrite_plan.cc" \
    "$ROOT/pmux_project/pass/case12_rewrite.cc" \
    "$ROOT/pmux_project/pass/case34_rewrite.cc" \
    "$ROOT/pmux_project/analyzer/readonly_analyzer.cc" \
    "$ROOT/pmux_project/analyzer/case12_proof_analyzer.cc" \
    "$ROOT/pmux_project/analyzer/case34_proof_analyzer.cc" \
    "$ROOT/pmux_project/analyzer/production_case34_proof_analyzer.cc" \
    "$ROOT/pmux_project/analyzer/decision_cone_proof_analyzer.cc" \
    -I"$ROOT/pmux_project/include" \
    -I"$ROOT/pmux_project/pass" \
    -I"$ROOT/pmux_project/analyzer"

cp "$PLUGIN" "$ROOT/pmux_project/pass/pmux_opt.so"

python3 "$ROOT/pmux_project/experiments/case12_v02/scripts/run_default_selection_validation.py" \
    --tool-root "$TOOL_ROOT" \
    --official "$OFFICIAL_DIR" \
    --out "$OUT/evidence" \
    --eqy

python3 - "$OUT/evidence/summary.json" <<'PY'
import json
import sys
from pathlib import Path

summary = json.loads(Path(sys.argv[1]).read_text())
if summary.get("passed") != 47 or summary.get("total") != 47:
    raise SystemExit("release validation did not reach 47/47")
for case in summary.get("official", []):
    if not case["variants"]["default"].get("pass"):
        raise SystemExit(f"official case{case['case']} default failed")
print("RELEASE_VALIDATION 47/47 PASS")
PY

sha256sum "$PLUGIN" > "$OUT/plugin.sha256"
git rev-parse HEAD > "$OUT/git-head.txt"
echo "release evidence: $OUT"
