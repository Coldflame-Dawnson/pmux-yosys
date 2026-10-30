#!/usr/bin/env python3
"""Independent A-side structural audit for B commit 504df2a389bf20d09e3bdaae64528caacc0c7bcf.

This script does not alter production sources.  It generates small RTLIL/Verilog
fixtures, runs the production plugin, and checks whether each candidate is
accepted or rejected according to the frozen two-person audit contract.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path


SUMMARY = re.compile(
    r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+) "
    r"removed_cells=(\d+) added_cells=(\d+)"
)


def run(command: list[str], cwd: Path) -> tuple[int, str]:
    completed = subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False
    )
    return completed.returncode, completed.stdout


def priority_fixture(branches: int, width: int, *, default: int = 0,
                     module_tap_depth: int | None = None,
                     cell_tap_depth: int | None = None) -> str:
    ports = [f"input [{branches-1}:0] s", f"input [{branches*width-1}:0] d",
             f"output [{width-1}:0] y"]
    if module_tap_depth is not None:
        ports.append(f"output [{width-1}:0] tap")
    lines = ["module top(" + ", ".join(ports) + ");"]
    previous = f"{width}'d{default}"
    for index in range(branches):
        lines.append(
            f"  wire [{width-1}:0] m{index} = s[{index}] ? "
            f"d[{index*width} +: {width}] : {previous};"
        )
        previous = f"m{index}"
    lines.append(f"  assign y = {previous};")
    if module_tap_depth is not None:
        lines.append(f"  assign tap = m{module_tap_depth};")
    if cell_tap_depth is not None:
        lines.append(f"  wire [{width-1}:0] sink = ~m{cell_tap_depth};")
        lines.append("  (* keep *) wire keep_sink = ^sink;")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def paired_pmux_rtlil(width: int, *, extra_decode_output: bool = False,
                      missing_code: int | None = None,
                      duplicate_code: tuple[int, int] | None = None,
                      swap_first_codes: tuple[int, int] | None = None) -> str:
    """Generate two 7-way PMUXes over a 3-bit state.

    The even/odd cross relation is constructed symbolically.  State code 1 is
    intentionally unconstrained because the production rewrite preserves each
    original code-1 data slice directly.
    """
    state_width = 3
    codes = list(range(1, 8))
    physical_codes = codes.copy()
    if missing_code is not None:
        physical_codes[missing_code - 1] = 0
    if duplicate_code is not None:
        dst, src = duplicate_code
        physical_codes[dst - 1] = physical_codes[src - 1]

    first = {code: f"\\f{code}" for code in codes}
    second = {code: f"\\g{code}" for code in codes}
    for n in range(1, 4):
        first[2*n] = f"\\x{n}"
        second[2*n+1] = f"\\x{n}"
        second[2*n] = f"\\y{n}"
        first[2*n+1] = f"\\y{n}"
    if swap_first_codes is not None:
        a, b = swap_first_codes
        first[a], first[b] = first[b], first[a]

    wires = {"\\y0", "\\y1", "\\state", "\\sel"}
    wires.update(first.values()); wires.update(second.values())
    lines = ["autoidx 1", "module \\top"]
    for name in sorted(wires):
        if name == "\\state":
            lines.append("  wire width 3 input 1 \\state")
        elif name == "\\sel":
            lines.append("  wire width 7 \\sel")
        else:
            lines.append(f"  wire width {width} {name}")
    lines.append(f"  wire width {width} input 2 \\a0")
    lines.append(f"  wire width {width} input 3 \\a1")
    lines.append(f"  wire width {width} output 4 \\out0")
    lines.append(f"  wire width {width} output 5 \\out1")
    if extra_decode_output:
        lines.append("  wire output 6 \\decode_tap")

    for index, code in enumerate(physical_codes):
        lines += [
            f"  cell $eq \\eq{index}",
            "    parameter \\A_SIGNED 0",
            "    parameter \\B_SIGNED 0",
            "    parameter \\A_WIDTH 3",
            "    parameter \\B_WIDTH 3",
            "    parameter \\Y_WIDTH 1",
            "    connect \\A \\state",
            f"    connect \\B 3'{code:03b}",
            f"    connect \\Y \\sel [{index}]",
            "  end",
        ]

    b0 = " ".join(first[code] for code in reversed(codes))
    b1 = " ".join(second[code] for code in reversed(codes))
    for name, a, b, y in (("\\p0", "\\a0", b0, "\\y0"),
                          ("\\p1", "\\a1", b1, "\\y1")):
        lines += [
            f"  cell $pmux {name}",
            f"    parameter \\WIDTH {width}",
            "    parameter \\S_WIDTH 7",
            f"    connect \\A {a}",
            f"    connect \\B {{ {b} }}",
            "    connect \\S \\sel",
            f"    connect \\Y {y}",
            "  end",
        ]
    lines += ["  connect \\out0 \\y0", "  connect \\out1 \\y1"]
    if extra_decode_output:
        lines.append("  connect \\decode_tap \\sel [0]")
    lines.append("end")
    return "\n".join(lines) + "\n"


def parse_summary(output: str) -> tuple[int, int, int, int, int]:
    found = SUMMARY.search(output)
    if not found:
        raise AssertionError("missing v02_case12 summary")
    return tuple(map(int, found.groups()))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--yosys", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo, output = args.repo.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    plugin = repo / "pmux_project/pass/pmux_opt.so"
    if not plugin.is_file():
        raise SystemExit(f"MISSING plugin: {plugin}")

    tests: list[tuple[str, str, str, tuple[int, int, int, int, int]]] = []
    for width in (1, 12, 32):
        tests.append((f"priority_valid_w{width}", ".v",
                      priority_fixture(4, width), (0, 1, 1, 4, 3*width)))
    tests += [
        ("priority_three_branch", ".v", priority_fixture(3, 1), (0, 0, 0, 0, 0)),
        ("priority_five_branch_must_reject", ".v", priority_fixture(5, 1), (0, 0, 0, 0, 0)),
        ("priority_nonzero_default", ".v", priority_fixture(4, 1, default=1), (0, 0, 0, 0, 0)),
        ("priority_module_output_tap", ".v", priority_fixture(4, 1, module_tap_depth=1), (0, 0, 0, 0, 0)),
        ("priority_extra_cell_fanout", ".v", priority_fixture(4, 1, cell_tap_depth=1), (0, 0, 0, 0, 0)),
    ]
    for width in (1, 12, 32):
        tests.append((f"paired_valid_w{width}", ".il", paired_pmux_rtlil(width),
                      (1, 0, 1, 2, 10)))
    tests += [
        ("paired_missing_code", ".il", paired_pmux_rtlil(1, missing_code=7), (0, 0, 0, 0, 0)),
        ("paired_duplicate_code", ".il", paired_pmux_rtlil(1, duplicate_code=(7, 6)), (0, 0, 0, 0, 0)),
        ("paired_b_slice_swap", ".il", paired_pmux_rtlil(1, swap_first_codes=(2, 4)), (0, 0, 0, 0, 0)),
        ("paired_comparator_module_output_fanout", ".il",
         paired_pmux_rtlil(1, extra_decode_output=True), (0, 0, 0, 0, 0)),
    ]

    rows = []
    fixture_dir = output / "fixtures"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case12-a-audit-") as temp:
        tempdir = Path(temp)
        for name, suffix, source, expected in tests:
            design = tempdir / f"{name}{suffix}"
            design.write_text(source, encoding="utf-8")
            (fixture_dir / design.name).write_text(source, encoding="utf-8")
            reader = "read_rtlil" if suffix == ".il" else "read_verilog"
            prepare = "" if suffix == ".il" else "prep -top top;"
            command = [str(args.yosys), "-m", str(plugin), "-p",
                       f"{reader} {design}; {prepare} pmux_opt -v02-case12 -no-v02-case34; check"]
            rc, text = run(command, repo)
            (output / f"{name}.log").write_text(text, encoding="utf-8")
            actual = parse_summary(text) if rc == 0 else None
            passed = rc == 0 and actual == expected
            rows.append({"name": name, "expected": expected, "actual": actual,
                         "returncode": rc, "passed": passed})
            print(("PASS" if passed else "FAIL"), name, "expected", expected, "actual", actual)

    (output / "summary.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    failures = [row for row in rows if not row["passed"]]
    print(f"TOTAL={len(rows)} FAILURES={len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
