#!/usr/bin/env python3
"""Role C static identity-dispatch scan; manual review covers dynamic proof IDs."""
import argparse
import json
import re
from pathlib import Path
PROJECT = Path(__file__).resolve().parents[1]
SOURCES = [
    "pass/case34_rewrite.cc", "pass/pmux_opt.cc", "pass/run_synth_intel_pmux.py",
    "analyzer/decision_cone_proof_analyzer.cc",
    "analyzer/production_case34_proof_analyzer.cc",
]
PATTERNS = {
    "official_case_literal": r"\b(?:test[1-4]|case[1-4](?!\d))\b",
    "automatic_cell_literal": r"\$procmux\$|\$memory\$auto\$|simple\.cc:[0-9]+",
    "fixed_table_row_literal": r"1001110000",
    "source_position_dispatch": r"ID\(src\)|ID::src|get_string_attribute\([^)]*src|source_line|line_number",
    "fixed_split_vector": r"\{\s*3\s*,\s*3\s*,\s*0\s*,\s*0\s*,\s*0\s*,\s*4\s*,\s*0\s*,\s*0\s*,\s*0\s*,\s*0\s*\}",
}
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    found = []
    dynamic_bindings = []
    for source in SOURCES:
        path = PROJECT / source
        for no, line in enumerate(path.read_text().splitlines(), 1):
            for label, pattern in PATTERNS.items():
                if re.search(pattern, line):
                    found.append({"file": source, "line": no, "pattern": label, "text": line.strip()})
            if "module->name" in line or "cell->name" in line or "anchor_cell" in line:
                dynamic_bindings.append({"file": source, "line": no, "text": line.strip()})
    result = {"scope": SOURCES, "forbidden_literal_hits": found,
              "dynamic_identity_binding_sites_for_manual_review": dynamic_bindings,
              "pass": not found}
    args.out.resolve().write_text(json.dumps(result, indent=2) + "\n")
    print(f"forbidden_literal_hits={len(found)} dynamic_binding_sites={len(dynamic_bindings)}")
    if found:
        raise SystemExit(1)
if __name__ == "__main__":
    main()
