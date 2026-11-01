#!/usr/bin/env python3
"""B implementation checks for the unified default dispatcher and native flow.

All case-specific counts below are acceptance observations, never runtime rules.
The native plugin loads the current synth_intel.cc into frozen Yosys 0.69.
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
from pathlib import Path

from run_case12_validation import fixture, paired_pmux_fixture

ROOT = Path(__file__).resolve().parents[4]
HASHES = (
    "2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13",
    "e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146",
    "1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9",
    "c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7",
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool-root", type=Path, required=True)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--eqy", action="store_true", help="prove the four default native gates")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tool = args.tool_root.resolve()
    official = args.official.resolve()
    for index, expected in enumerate(HASHES, 1):
        actual = hashlib.sha256((official / f"test{index}.v").read_bytes()).hexdigest()
        if actual != expected:
            raise SystemExit(f"official input hash mismatch: test{index}.v")
    env = os.environ.copy()
    env["PATH"] = str(tool / "bin") + os.pathsep + env.get("PATH", "")
    records = []

    def run(command, name, *, success=True):
        command = list(map(str, command))
        (out / f"{name}.command.txt").write_text(shlex.join(command) + "\n")
        completed = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   timeout=900)
        (out / f"{name}.log").write_text(completed.stdout)
        if (completed.returncode == 0) != success:
            raise RuntimeError(f"unexpected return code {completed.returncode}: {name}")
        return completed.stdout

    plugin = ROOT / "pmux_project/pass/pmux_opt.so"
    native = out / "native_synth.so"
    run([tool / "bin/yosys-config", "--build", native,
         ROOT / "pmux_project/experiments/case34_v02b/scripts/native_synth_loader.cc",
         f"-I{ROOT}"], "build_native")
    # Keep runtime files local; do not overwrite the frozen installation.
    runtime = out / "runtime"
    (runtime / "bin").mkdir(parents=True, exist_ok=True)
    (runtime / "share").mkdir(exist_ok=True)
    for executable in ("yosys", "yosys-abc"):
        shutil.copy2(tool / "bin" / executable, runtime / "bin" / executable)
    shutil.copytree(tool / "share/yosys", runtime / "share/yosys", dirs_exist_ok=True)
    for filename in ("pmux_case34_lut_map.v", "pmux_case34_abc.script"):
        shutil.copy2(ROOT / "techlibs/intel/common" / filename,
                     runtime / "share/yosys/intel/common" / filename)
    yosys = runtime / "bin/yosys"
    base = [yosys, "-Q", "-m", plugin]
    native_base = [*base, "-m", native]

    def record(name, passed):
        records.append({"test": name, "pass": bool(passed)})
        print(f"{'PASS' if passed else 'FAIL'} {name}", flush=True)

    def summary(text):
        match = re.search(r"v02_case12 paired=(\d+) priority=(\d+) committed=(\d+) "
                          r"removed_cells=(\d+) added_cells=(\d+)", text)
        return tuple(map(int, match.groups())) if match else None

    priority = out / "priority.v"
    priority.write_text(fixture(4, 1))
    for name, options, expected in (
        ("default", "", (0, 1, 1, 4, 3)),
        ("explicit", "-v02-case12", (0, 1, 1, 4, 3)),
        ("disable34_only", "-no-v02-case34", (0, 1, 1, 4, 3)),
        ("disable12_only", "-no-v02-case12", None),
        ("all_disabled", "-no-v02-case12 -no-v02-case34", None),
        ("stub_isolation", "-stub-unknown", None),
        ("explicit_stub_isolation", "-v02-case12 -stub-unknown", None),
    ):
        text = run([*base, "-p", f"read_verilog {priority}; prep -top top; "
                    f"pmux_opt {options}; stat"], name)
        passed = summary(text) == expected
        if expected is None:
            passed &= "cycloneiv_lcell_comb" not in text
        record(name, passed)
    text = run([*base, "-p", "pmux_opt -v02-case12 -no-v02-case12"],
               "conflicting_case12_flags", success=False)
    record("conflicting_case12_flags", "mutually exclusive" in text)

    malformed = out / "malformed.il"
    malformed.write_text(paired_pmux_fixture(exposed_decode=False, mismatched_a_width=True))
    text = run([*base, "-p", f"read_rtlil {malformed}; pmux_opt; design -reset"],
               "default_malformed_width")
    record("default_malformed_width", summary(text) == (0, 0, 0, 0, 0)
           and "eq_port_width_mismatch" in text and "writes=0" in text)

    for name, options in (
        ("native_case12_conflict", "-pmux-v02-case12 -no-pmux-v02-case12"),
        ("native_global_conflict", "-pmux-v02-case12 -no-pmux-v02"),
        ("native_wrong_family", "-family max10 -pmux-v02-case12"),
    ):
        text = run([*native_base, "-p", f"synth_intel {options}"], name, success=False)
        record(name, "mutually exclusive" in text or "requires -family cycloneiv" in text)

    for family in ("max10", "cycloneive", "cyclone10lp"):
        text = run([*native_base, "-p", f"read_verilog {priority}; "
                    f"synth_intel -family {family} -top top -run begin:map_ffs; stat"],
                   f"family_{family}")
        record(f"family_{family}", summary(text) is None and
               "v02_case34" in text and "cycloneiv_lcell_comb" not in text)

    formal = out / "lcell_formal.v"
    formal.write_text("""module cycloneiv_lcell_comb(dataa,datab,datac,datad,cin,combout);
parameter [15:0] lut_mask = 16'hffff;
parameter sum_lutc_input = "datac";
input dataa,datab,datac,datad,cin;
output combout;
assign combout = lut_mask[{datad,datac,datab,dataa}];
endmodule
""")
    official_results = []
    expected_defaults = ((182, 0, 222), (0, 15, 0), (11, 0, 12), (27, 0, 10))
    expected_baselines = ((288, 0, 222), (25, 0, 0), (13, 0, 17), (36, 0, 10))
    for index in range(1, 5):
        top = f"test{index}"
        source = official / f"{top}.v"
        variant_results = {}
        for variant, option in (("default", ""), ("explicit", "-pmux-v02-case12"),
                                ("rollback", "-no-pmux-v02")):
            prefix = out / f"case{index}_{variant}"
            # Stop after ABC9, then map the same result to vendor primitives.
            text = run([*native_base, "-p", f"read_verilog {source}; "
                        f"synth_intel -family cycloneiv -top {top} {option} -run begin:map_cells; "
                        f"write_json {prefix}.json; select {top}; write_rtlil -selected {prefix}.il; "
                        f"select *; synth_intel -family cycloneiv -top {top} {option} -run map_cells:check; "
                        f"write_json {prefix}_vendor.json"], f"case{index}_{variant}")
            module = json.loads(prefix.with_suffix(".json").read_text())["modules"][top]
            counts = {}
            for cell in module["cells"].values():
                counts[cell["type"]] = counts.get(cell["type"], 0) + 1
            observed = (counts.get("$lut", 0), counts.get("cycloneiv_lcell_comb", 0),
                        counts.get("dffeas", 0))
            expected = expected_baselines[index - 1] if variant == "rollback" else expected_defaults[index - 1]
            profile = re.findall(r"synth_intel: pmux_profile=(\w+)", text)
            expected_profile = "accepted_constant_table" if index == 4 and variant != "rollback" else "stock"
            passed = observed == expected and profile == [expected_profile]
            record(f"case{index}_{variant}_native_counts_profile", passed)
            variant_results[variant] = {"counts": counts, "observed_lut_vendor_dff": observed,
                                        "profile": profile, "pass": passed}
        if args.eqy:
            config = out / f"case{index}.eqy"
            config.write_text(f"""[gold]
read_verilog -formal {source}

[gate]
read_verilog -formal {formal}
read_rtlil {out / f'case{index}_default.il'}
read_verilog -formal -overwrite {ROOT / 'pmux_project/official_baseline/environment/dffeas_formal.v'}

[script]
hierarchy -check -top {top}
prep
memory_map
async2sync

[partition {top}]
merge *

[strategy sat]
use sat
depth 20

[strategy prove]
use sby
engine abc pdr
timeout 300
""")
            work = out / f"case{index}_eqy"
            run([tool / "bin/eqy", "-f", "-d", work, config], f"case{index}_eqy")
            record(f"case{index}_whole_design_eqy", (work / "PASS").is_file())
        official_results.append({"case": index, "variants": variant_results})

    result = {"scope": "B implementation selftest; pending A independent audit",
              "tests": records, "passed": sum(row["pass"] for row in records),
              "total": len(records), "official": official_results}
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"DEFAULT_SELECTION {result['passed']}/{result['total']} PASS")
    return 0 if result["passed"] == result["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
