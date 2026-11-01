#!/usr/bin/env python3
"""Independent option, isolation, family, and repeated-call dispatcher audit."""

import argparse
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def run(cmd, cwd, log, env=None):
    p = subprocess.run([str(x) for x in cmd], cwd=cwd, env=env, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_text(p.stdout, encoding="utf-8")
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool-root", type=Path, required=True)
    ap.add_argument("--official", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    tool, official, out = args.tool_root.resolve(), args.official.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    priority = out / "priority.v"
    priority.write_text("""module top(input [3:0] s,input [3:0] d,output y);
wire m0=s[0]?d[0]:1'b0; wire m1=s[1]?d[1]:m0;
wire m2=s[2]?d[2]:m1; wire m3=s[3]?d[3]:m2; assign y=m3; endmodule
""", encoding="utf-8")
    plugin = ROOT / "pmux_project/pass/pmux_opt.so"
    yosys = tool / "bin/yosys"
    base = [yosys, "-Q", "-m", plugin]
    rows = []

    def record(name, passed, **extra):
        rows.append({"test": name, "pass": bool(passed), **extra})
        print("PASS" if passed else "FAIL", name)

    variants = (
        ("default", "", (0, 1, 1, 4, 3), True),
        ("explicit", "-v02-case12", (0, 1, 1, 4, 3), True),
        ("no12", "-no-v02-case12", None, True),
        ("no34", "-no-v02-case34", (0, 1, 1, 4, 3), False),
        ("all_off", "-no-v02-case12 -no-v02-case34", None, False),
    )
    for name, opts, expected, expect34 in variants:
        p = run([*base, "-p", f"read_verilog {priority}; prep -top top; pmux_opt {opts}; check"],
                ROOT, out / f"pass_{name}.log")
        m = re.search(r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+) removed_cells=(\d+) added_cells=(\d+)", p.stdout)
        actual = tuple(map(int, m.groups())) if m else None
        has34 = "pmux_opt: v02_case34" in p.stdout
        record(f"pass_{name}", p.returncode == 0 and actual == expected and has34 == expect34,
               summary=actual, case34=has34)

    for family, a, b in (("case12", "-v02-case12", "-no-v02-case12"),
                         ("case34", "-v02-case34", "-no-v02-case34")):
        for order, opts in (("enable_disable", f"{a} {b}"), ("disable_enable", f"{b} {a}")):
            p = run([*base, "-p", f"read_verilog {priority}; prep -top top; pmux_opt {opts}"],
                    ROOT, out / f"conflict_{family}_{order}.log")
            no_structural = "pmux_opt: v02_case12 paired=" not in p.stdout and "pmux_opt: v02_case34" not in p.stdout
            record(f"conflict_{family}_{order}", p.returncode != 0 and "mutually exclusive" in p.stdout and no_structural)

    for stub in ("unknown", "zero", "one", "alternating"):
        p = run([*base, "-p", f"read_verilog {priority}; prep -top top; pmux_opt -v02-case12 -stub-{stub}"],
                ROOT, out / f"stub_{stub}.log")
        isolated = "pmux_opt: v02_case12 paired=" not in p.stdout and "pmux_opt: v02_case34" not in p.stdout
        record(f"stub_{stub}_isolation", p.returncode == 0 and isolated)

    p = run([*base, "-p", f"read_verilog {official/'test3.v'}; synth_intel -family cycloneiv -top test3 -run begin:map_ffram; opt -fast -mux_undef -undriven -fine -full; memory_map; opt -undriven -fine; pmux_opt -v02-case12 -test-decision-proof-mutations"], ROOT, out / "decision_mutation_explicit.log")
    isolated = "passed=26 checks=26 failures=0" in p.stdout and "pmux_opt: v02_case12 paired=" not in p.stdout and "pmux_opt: v02_case34" not in p.stdout
    record("decision_mutation_explicit_isolation", p.returncode == 0 and isolated)

    native = out / "native_synth.so"
    build = run([tool / "bin/yosys-config", "--build", native,
                 ROOT / "pmux_project/experiments/case34_v02b/scripts/native_synth_loader.cc", f"-I{ROOT}"],
                ROOT, out / "build_native.log")
    if build.returncode:
        raise SystemExit("native loader build failed")
    native_base = [*base, "-m", native]
    for family in ("max10", "cycloneive", "cyclone10lp"):
        p = run([*native_base, "-p", f"read_verilog {priority}; synth_intel -family {family} -top top -run begin:map_ffs; stat"], ROOT, out / f"family_{family}.log")
        record(f"family_{family}_no_cycloneiv", p.returncode == 0 and "cycloneiv_lcell_comb" not in p.stdout and "v02_case12 paired=" not in p.stdout)
    for order, opts in (("enable_disable", "-pmux-v02-case12 -no-pmux-v02-case12"),
                        ("disable_enable", "-no-pmux-v02-case12 -pmux-v02-case12")):
        p = run([*native_base, "-p", f"synth_intel -family cycloneiv {opts}"], ROOT, out / f"native_conflict_{order}.log")
        record(f"native_conflict_{order}", p.returncode != 0 and "mutually exclusive" in p.stdout)

    repeated = (
        f"read_verilog {priority}; synth_intel -family cycloneiv -top top -run begin:map_ffs; "
        "design -reset; "
        f"read_verilog {priority}; synth_intel -family cycloneiv -top top -no-pmux-v02-case12 -run begin:map_ffs; "
        "design -reset; "
        f"read_verilog {priority}; synth_intel -family max10 -top top -run begin:map_ffs"
    )
    p = run([*native_base, "-p", repeated], ROOT, out / "repeated_calls.log")
    summaries = re.findall(r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+)", p.stdout)
    # The full synth front half may simplify this tiny chain before PMUX_OPT.
    # State isolation is established by exactly one case12 invocation marker:
    # the first Cyclone IV default call, not the disabled or max10 calls.
    record("repeated_call_state_reset", p.returncode == 0 and len(summaries) == 1,
           case12_summaries=summaries)

    summary = {"tests": rows, "passed": sum(x["pass"] for x in rows), "total": len(rows)}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"DISPATCH_AUDIT {summary['passed']}/{summary['total']}")
    return 0 if summary["passed"] == summary["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
