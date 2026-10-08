#!/usr/bin/env python3
"""Role C independent hard-gate assertions over newly generated evidence."""
import argparse
import hashlib
import json
import re
from pathlib import Path
PROJECT = Path(__file__).resolve().parents[1]
EXPECTED = {"test1": (288, 222), "test2": (25, 0),
            "test3": (11, 12), "test4": (27, 10)}
B_GATE_DIGESTS = {
    "test3": "62b9aeb533472d61a1230df4525a2fc2f57e957188256c3d4b8e8cfd10a2a2ec",
    "test4": "b6a2d90c49c8522efb54bbe69603a9c6649935eadfe3c6a82490830b1c77e30",
}
def load(path):
    return json.loads(path.read_text())
def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True, type=Path)
    args = p.parse_args()
    root = args.out.resolve()
    assert load(root / "toolchain.json")["tested_b_commit"] == "99dee059077e83969cede6e292dc1ea9139389e3"
    mutation = (root / "mutation.log").read_text()
    match = re.search(r"decision proof mutation selftest passed=(\d+) checks=(\d+) failures=(\d+)", mutation)
    assert match and tuple(map(int, match.groups())) == (26, 26, 0)
    assert (root / "mutation_before.json").read_bytes() == (root / "mutation_after.json").read_bytes()
    assert "Found and reported 0 problems." in mutation
    selftest = {}
    for mode, expected in (("fast", (2116,29,60,50,81)), ("slow", (2118,29,60,50,81))):
        text = (root / ("selftest_" + mode + ".log")).read_text()
        counts = tuple(int(x) for x in re.findall(r"selftest: all checks passed \((\d+) assertions\)", text))
        assert counts == expected, (mode, counts)
        selftest[mode] = counts
    micro = load(root / "micro/summary.json")
    assert micro["counts"] == {"PASS": 19, "FAIL": 0, "SKIP": 0}
    atomic = load(root / "atomic/summary.json")
    assert all(atomic[x] == "PASS" for x in ("status", "structure", "check", "eqy"))
    random = load(root / "random_audit.json")
    assert all(random[x]["count"] == random[x]["pass"] == 1000 and random[x]["fail"] == 0
               for x in ("default", "v02"))
    assert random["v02"]["no_candidate_count"] == 1000
    assert random["v02"]["structural_match_seeds"] == []
    official = load(root / "official_audit.json")
    matrix = load(root / "official_matrix/summary.json")
    assert len(matrix["cases"]) == 4
    gate_results = {}
    for top, expected in EXPECTED.items():
        row = official[top]
        final = row["final"]
        assert (final["lut"], final["dff"]) == expected
        assert final["eqy_pass"]
        assert final["io_bits"]["inout"] == 0
        assert final["io_bits"] == row["variants"]["baseline"]["io_bits"]
        assert all(v["eqy_pass"] for v in row["variants"].values())
        assert len(row["variants"]) == 3
        gate_results[top] = {"selected": row["selected"], "lut": final["lut"],
                             "dff": final["dff"], "io_bits": final["io_bits"],
                             "total_cells": final["total_cells"],
                             "eqy": "PASS", "gate_sha256": final["gate_sha256"]}
    insertion = load(root / "insertion_audit.json")
    assert len(insertion["test3"]["added"]) == 1
    assert insertion["test3"]["sequential_control_changes"] == []
    assert len(insertion["test3"]["comparator_changes"]) == 3
    assert len(insertion["test4"]["removed"]) == 16
    assert len(insertion["test4"]["added"]) == 26
    assert insertion["test4"]["sequential_control_changes"] == []
    assert load(root / "identity_audit.json")["pass"]
    rtl = (PROJECT / "pmux_case/competition_case/test4.v").read_text()
    source_rows = {}
    for line in rtl.splitlines():
        m = re.search(r"5'd(\d+):.*?<=\s*\{6'b([01]{6}),1'b([01]),1'b([01]),1'b([01]),1'b([01])\};", line)
        if m:
            source_rows[int(m.group(1))] = "".join(m.groups()[1:])
    proof_text = (root / "production_case4_proof.log").read_text()
    proof_rows = {int(a): b for a,b in re.findall(r"PRODUCTION_TABLE_ROW address=(\d+) bits=([01]{10})", proof_text)}
    assert sorted(source_rows) == sorted(proof_rows) == list(range(32))
    assert source_rows == proof_rows and proof_rows[0] == "1001110000"
    result = {
        "schema": 1, "tested_b_commit": "99dee059077e83969cede6e292dc1ea9139389e3",
        "mutation": {"passed": 26, "checks": 26, "failures": 0,
                     "whole_module_json_unchanged": True},
        "selftests": selftest, "micro": micro["counts"], "atomic": atomic,
        "fixed_seed": random, "official": gate_results,
        "official_all_generated_gates_eqy_pass": True,
        "case4_truth_table": {"address_width": 5, "output_width": 10, "rows": 32,
                              "address_zero": proof_rows[0], "official_source_matches_proof": True},
        "b_gate_digest_reference_only": B_GATE_DIGESTS,
        "identity_scan_pass": True,
        "verdict": {"P0": "P0_PASS", "P1": "P1_PASS", "P2": "P2_PASS",
                    "production_candidates": "PRODUCTION_CANDIDATES_ACCEPTED",
                    "G3": "BLOCKED_BY_UNIFIED_SELECTION_GATE"},
    }
    (root / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    (root / "first_failure.txt").write_text("NONE: all mandatory runs PASS.\n")
    print(json.dumps(result["verdict"], indent=2))
if __name__ == "__main__":
    main()
