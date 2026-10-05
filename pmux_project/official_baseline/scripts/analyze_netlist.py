#!/usr/bin/env python3
import argparse
import collections
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--top", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--validate-dffeas", action="store_true")
    args = parser.parse_args()

    data = json.loads(Path(args.json).read_text(encoding="utf-8"))
    if args.top not in data.get("modules", {}):
        raise SystemExit(f"top module {args.top!r} is absent from {args.json}")
    cells = data["modules"][args.top].get("cells", {})
    counts = collections.Counter(cell["type"] for cell in cells.values())

    dff_types = {
        "$dff", "$dffe", "$adff", "$adffe", "$sdff", "$sdffe",
        "$aldff", "$aldffe", "$ff", "dffeas",
    }
    dff_count = 0
    for cell in cells.values():
        cell_type = cell["type"]
        if cell_type in dff_types or cell_type.startswith("$_DFF") or "dff" in cell_type.lower():
            width = cell.get("parameters", {}).get("WIDTH", 1)
            if isinstance(width, str):
                try:
                    width = int(width, 2)
                except ValueError:
                    width = 1
            dff_count += int(width)

    io_count = sum(
        count for cell_type, count in counts.items()
        if cell_type in {"cycloneiv_io_ibuf", "cycloneiv_io_obuf", "cycloneiv_io_bidir"}
        or cell_type.startswith("$_INOUT_")
        or cell_type.startswith("$_INPUT_")
        or cell_type.startswith("$_OUTPUT_")
    )

    expected_constants = {
        "prn": ["1"],
        "asdata": ["0"],
        "aload": ["0"],
        "sclr": ["0"],
        "sload": ["0"],
    }
    violations = []
    if args.validate_dffeas:
        for name, cell in cells.items():
            if cell["type"] != "dffeas":
                continue
            conns = cell.get("connections", {})
            for port, expected in expected_constants.items():
                if conns.get(port) != expected:
                    violations.append(
                        f"{name}.{port}: expected {expected}, got {conns.get(port)}"
                    )
        if violations:
            raise SystemExit("unsupported dffeas instance(s):\n" + "\n".join(violations))

    sequential = set(dff_types)
    memory_prefixes = ("$mem",)
    major_comb = {
        cell_type: count
        for cell_type, count in sorted(counts.items())
        if cell_type not in sequential
        and not cell_type.startswith("$_DFF")
        and "dff" not in cell_type.lower()
        and not cell_type.startswith(memory_prefixes)
    }
    result = {
        "top": args.top,
        "lut_count": counts.get("$lut", 0),
        "dff_count": dff_count,
        "io_count": io_count,
        "total_cells": len(cells),
        "pmux_count": counts.get("$pmux", 0),
        "mux_count": counts.get("$mux", 0),
        "eq_count": counts.get("$eq", 0),
        "ne_count": counts.get("$ne", 0),
        "and_count": counts.get("$and", 0),
        "or_count": counts.get("$or", 0),
        "xor_count": counts.get("$xor", 0),
        "not_count": counts.get("$not", 0),
        "major_comb_cells": major_comb,
        "cell_type_counts": dict(sorted(counts.items())),
        "dffeas_model_check": "PASS" if not violations else "FAIL",
    }
    Path(args.output).write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
