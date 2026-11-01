#!/usr/bin/env python3
"""Exhaustively check the case2 Cyclone IV LUT masks and priority order."""

import itertools
import json
import sys
from pathlib import Path


def lut(mask: int, dataa: int, datab: int, datac: int, datad: int) -> int:
    index = dataa | (datab << 1) | (datac << 2) | (datad << 3)
    return (mask >> index) & 1


def original(select: tuple[int, ...], data: tuple[int, ...]) -> int:
    value = 0
    for index in range(4):
        value = data[index] if select[index] else value
    return value


def rewritten(select: tuple[int, ...], data: tuple[int, ...]) -> int:
    w0 = lut(0xE4A0, select[1], select[0], data[1], data[0])
    w1 = lut(0x00D8, select[2], data[2], w0, 0)
    return lut(0x00D8, select[3], data[3], w1, 0)


def main() -> int:
    rows = []
    failures = []
    for select in itertools.product((0, 1), repeat=4):
        for data in itertools.product((0, 1), repeat=4):
            expected = original(select, data)
            actual = rewritten(select, data)
            row = {"select_lsb_first": select, "data_lsb_first": data,
                   "expected": expected, "actual": actual}
            rows.append(row)
            if expected != actual:
                failures.append(row)
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("priority-mask-summary.json")
    output.write_text(json.dumps({"vectors": len(rows), "failures": failures}, indent=2) + "\n")
    print(f"PRIORITY_MASK_VECTORS={len(rows)} FAILURES={len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
