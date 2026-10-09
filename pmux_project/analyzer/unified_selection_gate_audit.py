#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, required=True)
    ap.add_argument("--tool", type=Path, required=True)
    ap.add_argument("--official", type=Path, required=True)
    ns = ap.parse_args()
    repo, tool, official = ns.repo.resolve(), ns.tool.resolve(), ns.official.resolve()
    yosys = tool / "bin/yosys"
    plugin = repo / "pmux_project/pass/pmux_opt.so"
    audit_plugin = repo / "pmux_project/analyzer/unified_selection_audit.so"
    lut_map = repo / "pmux_project/pass/pmux_case34_lut_map.v"
    abc_script = repo / "pmux_project/pass/pmux_case34_abc.script"
    env = dict(os.environ, YOSYS_DATDIR=str(tool / "share/yosys"))
    results = []

    def stage(case):
        top = f"test{case}"
        return [
            f"read_verilog {official / (top + '.v')}",
            f"synth_intel -family cycloneiv -top {top} -run begin:map_ffram",
            "opt -fast -mux_undef -undriven -fine -full",
            "memory_map",
            "opt -undriven -fine",
        ]

    def mapper(case):
        top = f"test{case}"
        return [
            "opt_clean",
            "techmap -map +/techmap.v",
            "opt -full",
            "clean -purge",
            "setundef -undriven -zero",
            f"pmux_opt_abc9 -family cycloneiv -top {top} "
            f"-cofactor-map {lut_map} -abc-script {abc_script}",
        ]

    def run(name, case, commands, expected_profiles, expect_structural=None):
        with tempfile.TemporaryDirectory(prefix="pmux-a-unified-") as td:
            work = Path(td)
            full = stage(case) + commands
            cmd = [str(yosys), "-Q", "-m", str(plugin), "-m", str(audit_plugin),
                   "-p", "; ".join(full)]
            cp = subprocess.run(cmd, cwd=work, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            log = cp.stdout
            profiles = [line.rsplit("=", 1)[1].strip() for line in log.splitlines()
                        if "pmux_opt_abc9: profile=" in line]
            structural = None
            plan = work / "plan.json"
            if plan.is_file():
                structural = len(json.loads(plan.read_text())["structural_edits"])
            ok = cp.returncode == 0 and profiles == expected_profiles
            if expect_structural is not None:
                ok = ok and structural == expect_structural
            results.append({
                "name": name, "pass": ok, "returncode": cp.returncode,
                "profiles": profiles, "structural_edits": structural,
            })
            if not ok:
                print(log)
        return results[-1]["pass"]

    def pmux(mode=""):
        suffix = f" {mode}" if mode else ""
        return f"pmux_opt{suffix} -write-plan plan.json"

    run("no_auth_no_cell", 1,
        [pmux("-no-v02-case34"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(1),
        ["stock"], 0)
    run("no_auth_existing_table_cell", 4,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table true",
         "scratchpad -set pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 1)
    run("forged_auth_no_cell", 1,
        [pmux("-no-v02-case34"),
         "scratchpad -set pmux_opt.accepted_constant_table true"] + mapper(1),
        ["stock"], 0)
    run("legal_case4", 4,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table true"] + mapper(4),
        ["accepted_constant_table"], 1)
    run("consume_then_repeat", 4,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table true"] +
        mapper(4) +
        ["scratchpad -assert pmux_opt.accepted_constant_table false"] +
        mapper(4),
        ["accepted_constant_table", "stock"], 1)
    run("consume_reintroduce_cell_then_repeat", 4,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table true"] +
        mapper(4) +
        ["scratchpad -assert pmux_opt.accepted_constant_table false",
         "pmux_audit_reintroduce_lut4"] +
        mapper(4),
        ["accepted_constant_table", "stock"], 1)
    run("explicit_disable_case4", 4,
        [pmux("-no-v02-case34"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 0)
    run("stub_mode_clears_stale", 4,
        ["scratchpad -set pmux_opt.accepted_constant_table true",
         pmux("-stub-unknown"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 0)
    run("mutation_mode_clears_stale", 3,
        ["scratchpad -set pmux_opt.accepted_constant_table true",
         "pmux_opt -test-decision-proof-mutations",
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(3),
        ["stock"])
    run("snapshot_mode_no_structure", 4,
        ["scratchpad -set pmux_opt.accepted_constant_table true",
         pmux("-stub-zero -test-snapshot-mismatch"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 0)
    run("proof_reject_keep", 4,
        ["setattr -set keep 1 t:\\$mux",
         pmux(), "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 0)
    run("case1_default", 1,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(1),
        ["stock"], 0)
    run("case2_default", 2,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(2),
        ["stock"], 0)
    run("case3_decision_cone", 3,
        [pmux(), "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(3),
        ["stock"], 1)
    run("compatibility_enable_case3", 3,
        [pmux("-v02-case34"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(3),
        ["stock"], 1)
    run("conflict_mode_no_structure", 4,
        ["scratchpad -set pmux_opt.accepted_constant_table true",
         pmux("-stub-zero -test-rewrite-conflict"),
         "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(4),
        ["stock"], 0)
    run("stale_auth_cleared_at_start", 1,
        ["scratchpad -set pmux_opt.accepted_constant_table true",
         pmux(), "scratchpad -assert pmux_opt.accepted_constant_table false"] + mapper(1),
        ["stock"], 0)

    passed = sum(item["pass"] for item in results)
    synth_source = (repo / "techlibs/intel/synth_intel.cc").read_text()
    read_marker = 'scratchpad_get_bool("pmux_opt.accepted_constant_table", false)'
    map_marker = 'run("techmap -map +/techmap.v")'
    clear_marker = 'scratchpad_set_bool("pmux_opt.accepted_constant_table", false)'
    read_at = synth_source.index(read_marker)
    map_at = synth_source.index(map_marker, read_at)
    synth_consumes = clear_marker in synth_source[read_at:map_at]
    print(json.dumps({"passed": passed, "checks": len(results),
                     "synth_intel_consumes_scratchpad": synth_consumes,
                     "results": results},
                     indent=2, sort_keys=True))
    return 0 if passed == len(results) else 1

if __name__ == "__main__":
    raise SystemExit(main())
