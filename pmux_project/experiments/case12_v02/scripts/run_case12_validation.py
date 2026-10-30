#!/usr/bin/env python3
"""Fast structural validation for the proof-gated case1/case2 production rules."""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path


def run(command: list[str], *, cwd: Path) -> str:
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, check=False)
    if completed.returncode:
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}\n{completed.stdout}")
    return completed.stdout


def fixture(branches: int, width: int, *, shared_tap: bool = False) -> str:
    lines = [f"module top(input [{branches-1}:0] s, input [{branches*width-1}:0] d, "
             f"output [{width-1}:0] y" + (f", output [{width-1}:0] tap" if shared_tap else "") + ");"]
    previous = f"{width}'b0"
    for index in range(branches):
        lines.append(f"  wire [{width-1}:0] m{index} = s[{index}] ? "
                     f"d[{index*width} +: {width}] : {previous};")
        previous = f"m{index}"
    lines.append(f"  assign y = {previous};")
    if shared_tap:
        lines.append("  assign tap = m1;")
    lines.append("endmodule")
    return "\n".join(lines) + "\n"


def paired_pmux_fixture(*, exposed_decode: bool, mismatched_a_width: bool = False) -> str:
    tap_wire = "  wire output 6 \\decode_tap\n" if exposed_decode else ""
    tap_connect = "  connect \\decode_tap \\s1\n" if exposed_decode else ""
    eq_cells = []
    for code in range(1, 8):
        a_width = 4 if mismatched_a_width and code == 1 else 3
        eq_cells.append(f"""  cell $eq \\eq{code}
    parameter \\A_SIGNED 0
    parameter \\A_WIDTH {a_width}
    parameter \\B_SIGNED 0
    parameter \\B_WIDTH 3
    parameter \\Y_WIDTH 1
    connect \\A \\state
    connect \\B 3'{code:03b}
    connect \\Y \\s{code}
  end""")
    return f"""autoidx 1
module \\top
  wire width 3 input 1 \\state
  wire input 2 \\a0
  wire input 3 \\a1
  wire output 4 \\y0
  wire output 5 \\y1
{tap_wire}  wire \\s1
  wire \\s2
  wire \\s3
  wire \\s4
  wire \\s5
  wire \\s6
  wire \\s7
{chr(10).join(eq_cells)}
  cell $pmux \\p0
    parameter \\S_WIDTH 7
    parameter \\WIDTH 1
    connect \\A \\a0
    connect \\B 7'1010100
    connect \\S {{ \\s7 \\s6 \\s5 \\s4 \\s3 \\s2 \\s1 }}
    connect \\Y \\y0
  end
  cell $pmux \\p1
    parameter \\S_WIDTH 7
    parameter \\WIDTH 1
    connect \\A \\a1
    connect \\B 7'0101011
    connect \\S {{ \\s7 \\s6 \\s5 \\s4 \\s3 \\s2 \\s1 }}
    connect \\Y \\y1
  end
{tap_connect}end
"""


def expect_summary(output: str, *, paired: int, priority: int, committed: int,
                   removed: int, added: int) -> None:
    match = re.search(r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+) "
                      r"removed_cells=(\d+) added_cells=(\d+)", output)
    if not match:
        raise AssertionError(f"case12 summary missing\n{output}")
    observed = tuple(map(int, match.groups()))
    expected = (paired, priority, committed, removed, added)
    if observed != expected:
        raise AssertionError(f"summary {observed} != {expected}\n{output}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--yosys", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    plugin = repo / "pmux_project/pass/pmux_opt.so"
    if not plugin.is_file():
        raise SystemExit(f"MISSING plugin: {plugin}; run make -C pmux_project/pass first")

    cases = [
        ("valid_priority_width1", "read_verilog", ".v", fixture(4, 1), "prep -top top", "", 0, 1, 1, 4, 3),
        ("valid_priority_width5", "read_verilog", ".v", fixture(4, 5), "prep -top top", "", 0, 1, 1, 4, 15),
        ("three_priority_branches", "read_verilog", ".v", fixture(3, 1), "prep -top top", "", 0, 0, 0, 0, 0),
        ("five_priority_branches", "read_verilog", ".v", fixture(5, 1), "prep -top top", "", 0, 0, 0, 0, 0),
        ("shared_priority_intermediate", "read_verilog", ".v", fixture(4, 1, shared_tap=True), "prep -top top", "", 0, 0, 0, 0, 0),
        ("protected_priority_muxes", "read_verilog", ".v", fixture(4, 1), "prep -top top", "select t:$mux; setattr -set keep 1; select *;", 0, 0, 0, 0, 0),
        ("paired_decode_module_output", "read_rtlil", ".il", paired_pmux_fixture(exposed_decode=True), "hierarchy -top top", "", 0, 0, 0, 0, 0),
        ("paired_decode_awidth_mismatch", "read_rtlil", ".il", paired_pmux_fixture(exposed_decode=False, mismatched_a_width=True), "hierarchy -top top", "", 0, 0, 0, 0, 0),
    ]
    with tempfile.TemporaryDirectory(prefix="pmux-case12-") as directory:
        work = Path(directory)
        for name, reader, suffix, source, prepare, setup, paired, priority, committed, removed, added in cases:
            design = work / f"{name}{suffix}"
            design.write_text(source, encoding="utf-8")
            # Yosys 0.69 check/stat themselves dereference inconsistent width
            # metadata on the intentionally malformed $eq fixture.  The
            # production pass must still return cleanly and report zero writes;
            # all well-formed fixtures continue through check and stat.
            post_validation = "" if name == "paired_decode_awidth_mismatch" else "; check; stat"
            output = run([str(args.yosys), "-m", str(plugin), "-p",
                          f"{reader} {design}; {prepare}; {setup} "
                          f"pmux_opt -v02-case12 -no-v02-case34{post_validation}"], cwd=repo)
            expect_summary(output, paired=paired, priority=priority, committed=committed,
                           removed=removed, added=added)
            print(f"PASS {name}")

    print(f"PASS all {len(cases)} case12 structural fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
