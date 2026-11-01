#!/usr/bin/env python3
"""Independent A audit for malformed case12 equality metadata."""

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


def paired_fixture() -> str:
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
  wire output 4 \\y0
  wire output 5 \\y1
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
end
"""


def replace_first(text: str, old: str, new: str) -> str:
    if text.count(old) < 1:
        raise AssertionError(f"missing mutation target: {old!r}")
    return text.replace(old, new, 1)


def mixed_fixture() -> str:
    text = paired_fixture()
    text = replace_first(text, "  wire \\s1\n", "  wire \\bad_y\n  wire \\s1\n")
    bad = """  cell $eq \\bad_eq
    parameter \\A_SIGNED 0
    parameter \\A_WIDTH 4
    parameter \\B_SIGNED 0
    parameter \\B_WIDTH 3
    parameter \\Y_WIDTH 1
    connect \\A \\state
    connect \\B 3'000
    connect \\Y \\bad_y
  end
"""
    return replace_first(text, "  cell $pmux \\p0\n", bad + "  cell $pmux \\p0\n")


def noncandidate_fixture() -> str:
    return """autoidx 1
module \\top
  wire width 3 input 1 \\state
  wire output 2 \\y
  cell $eq \\bad_eq
    parameter \\A_SIGNED 0
    parameter \\A_WIDTH 4
    parameter \\B_SIGNED 0
    parameter \\B_WIDTH 3
    parameter \\Y_WIDTH 1
    connect \\A \\state
    connect \\B 3'000
    connect \\Y \\y
  end
end
"""


def normalized_returncode(code: int) -> int:
    return 128 + (-code) if code < 0 else code


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    output = args.output.resolve()
    fixtures = output / "fixtures"
    logs = output / "logs"
    fixtures.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    plugin = repo / "pmux_project/pass/pmux_opt.so"

    base = paired_fixture()
    variants = {
        "a_width_mismatch": replace_first(base, "parameter \\A_WIDTH 3", "parameter \\A_WIDTH 4"),
        "b_width_mismatch": replace_first(base, "parameter \\B_WIDTH 3", "parameter \\B_WIDTH 4"),
        "y_width_mismatch": replace_first(base, "parameter \\Y_WIDTH 1", "parameter \\Y_WIDTH 2"),
        "missing_a_width": replace_first(base, "    parameter \\A_WIDTH 3\n", ""),
        "missing_b_width": replace_first(base, "    parameter \\B_WIDTH 3\n", ""),
        "missing_y_width": replace_first(base, "    parameter \\Y_WIDTH 1\n", ""),
        "valid_candidate_plus_malformed_noncandidate": mixed_fixture(),
        "malformed_noncandidate_only": noncandidate_fixture(),
    }

    rows: list[dict[str, object]] = []
    for name, source in variants.items():
        fixture = fixtures / f"{name}.il"
        fixture.write_text(source, encoding="utf-8")
        completed = run([
            str(args.yosys), "-Q", "-m", str(plugin), "-p",
            f"read_rtlil {fixture}; hierarchy -top top; "
            "pmux_opt; design -reset",
        ], repo)
        (logs / f"{name}.log").write_text(completed.stdout, encoding="utf-8")
        match = SUMMARY.search(completed.stdout)
        actual = tuple(map(int, match.groups())) if match else None
        reason = "reason scope=v02_case12 code=eq_port_width_mismatch count=1" in completed.stdout
        writes_zero = "writes=0" in completed.stdout
        no_general_analysis = "pmux_opt: summary modules=" not in completed.stdout
        passed = (
            completed.returncode == 0 and actual == (0, 0, 0, 0, 0)
            and reason and writes_zero and no_general_analysis
        )
        rows.append({
            "name": name,
            "returncode": normalized_returncode(completed.returncode),
            "actual": actual,
            "reason": reason,
            "writes_zero": writes_zero,
            "general_analysis_skipped": no_general_analysis,
            "passed": passed,
        })
        print("PASS" if passed else "FAIL", name, rows[-1])

    control_fixture = fixtures / "a_width_mismatch.il"
    control = run([
        str(args.yosys), "-Q", "-p",
        f"read_rtlil {control_fixture}; hierarchy -top top; check",
    ], repo)
    (logs / "control_without_plugin.log").write_text(control.stdout, encoding="utf-8")
    control_rc = normalized_returncode(control.returncode)
    control_passed = control_rc == 139
    result = {
        "variants": rows,
        "control_without_plugin": {
            "returncode": control_rc,
            "expected_yosys_069_crash": True,
            "passed": control_passed,
        },
    }
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    failures = [row for row in rows if not row["passed"]]
    print("CONTROL", "PASS" if control_passed else "FAIL", "returncode", control_rc)
    print(f"MATRIX={len(rows) - len(failures)}/{len(rows)} CONTROL={int(control_passed)}/1")
    return 0 if not failures and control_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
