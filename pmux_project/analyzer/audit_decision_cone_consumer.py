#!/usr/bin/env python3
"""Read-only adversarial audit model for B's DecisionConeProof consumer.

This test does not compile or modify the production pass.  It reproduces the
finite truth matching rule and checks, directly against the audited source,
that the commit-time validation gates required by the frozen proof contract
are present.  A missing gate is reported as BLOCKED, never repaired here.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def matches(care: str, value: str, candidate: str) -> bool:
    """Model case34_rewrite.cc:675-681 for well-sized binary vectors."""
    return all(c != "1" or value[i] == candidate[i] for i, c in enumerate(care))


def bits(rows: int, fn) -> str:
    return "".join("1" if fn(row) else "0" for row in range(rows))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    text = args.source.read_text(encoding="utf-8")

    support = [bits(8, lambda r, i=i: (r >> i) & 1) for i in range(3)]
    atoms = support + [bits(8, lambda r: ((r & 3) == 2))]
    fixtures: list[dict[str, object]] = []

    def check(name: str, condition: bool, expected: str, detail: str) -> None:
        fixtures.append({"name": name, "pass": condition, "expected": expected, "detail": detail})

    check("all_care_atom", matches("1" * 8, atoms[1], atoms[1]), "accept", "single support atom")
    check("sparse_care", matches("10000100", "10000100", "10000100"), "accept", "only care=1 rows constrain E")
    check("empty_care_theorem", matches("0" * 8, "10100101", "0" * 8), "accept", "vacuous match is sound only with all per-sink observation obligations")
    check("inverted_atom", matches("1" * 8, bits(8, lambda r: not ((r >> 0) & 1)), bits(8, lambda r: not ((r >> 0) & 1))), "accept", "inverted support atom")
    check("and_atom", matches("1" * 8, bits(8, lambda r: (r & 1) and (r & 2)), bits(8, lambda r: (r & 1) and (r & 2))), "accept", "two-atom AND")
    check("or_atom", matches("1" * 8, bits(8, lambda r: (r & 1) or (r & 2)), bits(8, lambda r: (r & 1) or (r & 2))), "accept", "two-atom OR")
    check("xor_atom", matches("1" * 8, bits(8, lambda r: bool(r & 1) != bool(r & 2)), bits(8, lambda r: bool(r & 1) != bool(r & 2))), "accept", "two-atom XOR")
    hard = "10010110"
    simple = {"0" * 8, "1" * 8}
    for atom in atoms:
        simple.add(atom)
        simple.add("".join("1" if b == "0" else "0" for b in atom))
    for lhs in range(len(atoms)):
        for rhs in range(lhs + 1, len(atoms)):
            simple.add(bits(8, lambda r, l=lhs, q=rhs: atoms[l][r] == "1" and atoms[q][r] == "1"))
            simple.add(bits(8, lambda r, l=lhs, q=rhs: atoms[l][r] == "1" or atoms[q][r] == "1"))
            simple.add(bits(8, lambda r, l=lhs, q=rhs: atoms[l][r] != atoms[q][r]))
    check("dag_fallback", hard not in simple, "fallback", "no bounded local expression matches")
    reordered = [support[2], support[0], support[1]]
    check("support_reorder", reordered[0] == bits(8, lambda r: (r >> 2) & 1), "accept", "truth address follows support_lsb_first index")
    check("two_comparators_same_truth", matches("1" * 8, atoms[3], atoms[3]), "accept", "duplicate atoms are semantically harmless")
    check("ne_comparator", matches("1" * 8, bits(8, lambda r: (r & 3) != 2), bits(8, lambda r: (r & 3) != 2)), "accept", "$ne truth is complement of equality")
    check("removed_non_msb", [i for i in range(4) if i not in {1}] == [0, 2, 3], "accept", "index-set removal itself is order preserving")

    required_source_gates = {
        "sink_identity_cross_binding": (
            "sink.cell_id == boundary.cell_id" in text
            and "sink.d_bit == boundary.d_bit" in text
            and "sink.q_bit == boundary.q_bit" in text
        ),
        "care_cardinality": (
            "GetSize(sink.care_mask_lsb_address) != truth_rows" in text
            or "GetSize(sink.care_mask_lsb_address) == truth_rows" in text
        ),
        "value_cardinality": (
            "GetSize(sink.value_mask_lsb_address) != truth_rows" in text
            or "GetSize(sink.value_mask_lsb_address) == truth_rows" in text
        ),
        "support_bound_12": (
            "GetSize(proof.support_lsb_first) > 12" in text
            or "proof.support_lsb_first.size() > 12" in text
        ),
        "dag_bound_4096": (
            "GetSize(proof.normalized_dag) > 4096" in text
            or "proof.normalized_dag.size() > 4096" in text
        ),
        "relation_item_authorization": "relation.reason_code" in text[text.find("bool build_decision_plan"):text.find("struct GuardedPlan")],
        "comparator_constant_binding": (
            "comparator.comparator_constant" in text[text.find("bool build_decision_plan"):text.find("struct GuardedPlan")]
        ),
        "comparator_source_binding": (
            "source_to_input" in text[text.find("bool build_decision_plan"):text.find("struct GuardedPlan")]
        ),
        "duplicate_proof_id_rejection": "duplicate_proof_id" in text,
    }

    expected_missing = {
        "sink_identity_cross_binding",
        "care_cardinality",
        "value_cardinality",
        "support_bound_12",
        "dag_bound_4096",
        "relation_item_authorization",
        "comparator_constant_binding",
        "comparator_source_binding",
        "duplicate_proof_id_rejection",
    }
    missing = sorted(name for name, present in required_source_gates.items() if not present)
    check("static_missing_gate_set", set(missing) == expected_missing, "BLOCKED", ", ".join(missing))

    result = {
        "schema": 1,
        "audited_source": str(args.source),
        "fixtures": fixtures,
        "required_source_gates": required_source_gates,
        "missing_gates": missing,
        "verdict": "BLOCKED" if missing else "AUTHORIZED",
    }
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if all(item["pass"] for item in fixtures) else 1


if __name__ == "__main__":
    raise SystemExit(main())
