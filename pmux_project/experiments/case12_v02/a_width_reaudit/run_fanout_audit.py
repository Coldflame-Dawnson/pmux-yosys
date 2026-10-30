#!/usr/bin/env python3
"""Independent case12 fanout-closure audit."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


SUMMARY = re.compile(
    r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+) "
    r"removed_cells=(\d+) added_cells=(\d+)"
)


def base_fixture() -> str:
    eqs = []
    for code in range(1, 8):
        eqs.append(f"""  cell $eq \\eq{code}
    parameter \\A_SIGNED 0
    parameter \\A_WIDTH 3
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
  wire input 4 \\a2
  wire output 5 \\y0
  wire output 6 \\y1
  wire output 7 \\y2
  wire \\s1
  wire \\s2
  wire \\s3
  wire \\s4
  wire \\s5
  wire \\s6
  wire \\s7
{chr(10).join(eqs)}
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
  cell $pmux \\p2
    parameter \\S_WIDTH 7
    parameter \\WIDTH 1
    connect \\A \\a2
    connect \\B 7'0000000
    connect \\S {{ \\s7 \\s6 \\s5 \\s4 \\s3 \\s2 \\s1 }}
    connect \\Y \\y2
  end
end
"""


def replace_first(text: str, old: str, new: str) -> str:
    if old not in text:
        raise AssertionError(f"missing target {old!r}")
    return text.replace(old, new, 1)


def insert_before_module_end(text: str, addition: str) -> str:
    head, tail = text.rsplit("end\n", 1)
    if tail:
        raise AssertionError("unexpected text after module end")
    return head + addition + "end\n"


def module_output_fixture() -> str:
    text = base_fixture()
    text = replace_first(text, "  wire \\s1\n", "  wire output 8 \\decode_tap\n  wire \\s1\n")
    return insert_before_module_end(text, "  connect \\decode_tap \\s1\n")


def unknown_consumer_fixture() -> str:
    text = base_fixture()
    unknown = """  cell \\unknown_cell \\u0
    connect \\A \\s1
  end
"""
    return insert_before_module_end(text, unknown)


def uncovered_deleted_pmux_port_fixture() -> str:
    return replace_first(base_fixture(), "    connect \\A \\a0\n", "    connect \\A \\s1\n")


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo, output = args.repo.resolve(), args.output.resolve()
    fixtures, logs = output / "fixtures", output / "logs"
    fixtures.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    production = repo / "pmux_project/pass/pmux_opt.so"
    report = output / "case12_audit_report.so"
    yosys_config = args.yosys.parent / "yosys-config"
    build = run([
        str(yosys_config), "--build", str(report),
        str(repo / "pmux_project/experiments/case12_v02/a_width_reaudit/case12_audit_report.cc"),
        str(repo / "pmux_project/analyzer/case12_proof_analyzer.cc"),
        "-I" + str(repo / "pmux_project/include"),
        "-I" + str(repo / "pmux_project/analyzer"),
    ], repo)
    (logs / "build_report.log").write_text(build.stdout, encoding="utf-8")
    if build.returncode != 0:
        print(build.stdout)
        return 1

    cases = {
        "preserved_third_pmux": (base_fixture(), (1, 0, 1, 2, 10), True),
        "module_output_reject": (module_output_fixture(), (0, 0, 0, 0, 0), False),
        "unknown_cell_port_reject": (unknown_consumer_fixture(), (0, 0, 0, 0, 0), False),
        "deleted_pmux_uncovered_port_reject": (
            uncovered_deleted_pmux_port_fixture(), (0, 0, 0, 0, 0), False
        ),
    }
    rows = []
    for name, (source, expected, preserved_reason) in cases.items():
        fixture = fixtures / f"{name}.il"
        fixture.write_text(source, encoding="utf-8")
        command = [
            str(args.yosys), "-Q", "-m", str(report), "-m", str(production), "-p",
            f"read_rtlil {fixture}; hierarchy -top top; case12_audit_report; "
            "pmux_opt -v02-case12 -no-v02-case34; check",
        ]
        completed = run(command, repo)
        (logs / f"{name}.log").write_text(completed.stdout, encoding="utf-8")
        match = SUMMARY.search(completed.stdout)
        actual = tuple(map(int, match.groups())) if match else None
        reason_ok = (
            "reason=proved_with_preserved_decode_fanout" in completed.stdout
            if preserved_reason else "CASE12_AUDIT_PAIRED" not in completed.stdout
        )
        check_ok = "Found and reported 0 problems." in completed.stdout
        passed = completed.returncode == 0 and actual == expected and reason_ok and check_ok
        row = {"name": name, "actual": actual, "expected": expected,
               "reason_ok": reason_ok, "check_ok": check_ok, "passed": passed}
        rows.append(row)
        print("PASS" if passed else "FAIL", row)
    (output / "summary.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    failures = [row for row in rows if not row["passed"]]
    print(f"FANOUT={len(rows)-len(failures)}/{len(rows)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
