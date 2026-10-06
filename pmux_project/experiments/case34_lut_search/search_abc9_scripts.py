#!/usr/bin/env python3
"""Search ABC9 scripts on the exact official case3/case4 synthesis flow."""

import argparse
import json
import subprocess
from pathlib import Path


ABC_SCRIPTS = {
    "default_equivalent": "&scorr; &sweep; &dc2; &dch -f -r; &ps; &if -W 300 -v; &mfs",
    "dc2_twice": "&scorr; &sweep; &dc2; &dc2; &dch -f -r; &if -W 300 -v; &mfs",
    "dch_twice": "&scorr; &sweep; &dc2; &dch -f -r; &dch -f -r; &if -W 300 -v; &mfs",
    "choice_loop": "&scorr; &sweep; &dc2; &dch -f -r; &dc2; &dch -f -r; &if -W 300 -v; &mfs",
    "heavy_dch": "&scorr; &sweep; &dc2; &dch -r; &if -W 300 -v; &mfs",
    "full_dch": "&scorr; &sweep; &dc2; &dch; &if -W 300 -v; &mfs",
    "dch_equiv": "&scorr; &sweep; &dc2; &dch -e -r; &if -W 300 -v; &mfs",
    "dch_equiv_no_r": "&scorr; &sweep; &dc2; &dch -e; &if -W 300 -v; &mfs",
    "dch_equiv_light": "&scorr; &sweep; &dc2; &dch -e -f -r; &if -W 300 -v; &mfs",
    "dch_equiv_minlevel": "&scorr; &sweep; &dc2; &dch -e -m -r; &if -W 300 -v; &mfs",
    "dch_equiv_gia": "&scorr; &sweep; &dc2; &dch -e -g -r; &if -W 300 -v; &mfs",
    "dch_equiv_csat": "&scorr; &sweep; &dc2; &dch -e -c -r; &if -W 300 -v; &mfs",
    "dch_equiv_x": "&scorr; &sweep; &dc2; &dch -e -x -r; &if -W 300 -v; &mfs",
    "dch_equiv_y": "&scorr; &sweep; &dc2; &dch -e -y -r; &if -W 300 -v; &mfs",
    "dch_equiv_x_minlevel": "&scorr; &sweep; &dc2; &dch -e -x -m -r; &if -W 300 -v; &mfs",
    "dch_equiv_y_minlevel": "&scorr; &sweep; &dc2; &dch -e -y -m -r; &if -W 300 -v; &mfs",
    "dch_equiv_twice": "&scorr; &sweep; &dc2; &dch -e -r; &dch -e -r; &if -W 300 -v; &mfs",
    "dch_equiv_loop": "&scorr; &sweep; &dc2; &dch -e -r; &dc2; &dch -e -r; &if -W 300 -v; &mfs",
    "dch_then_equiv": "&scorr; &sweep; &dc2; &dch -f -r; &dch -e -r; &if -W 300 -v; &mfs",
    "equiv_then_dch": "&scorr; &sweep; &dc2; &dch -e -r; &dch -f -r; &if -W 300 -v; &mfs",
    "dch_equiv_area_if": "&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs",
    "dch_equiv_area_if_twice": "&scorr; &sweep; &dc2; &dch -e -r; &if -a -W 300 -v; &mfs; &mfs",
    "dch_equiv_delay_500": "&scorr; &sweep; &dc2; &dch -e -r; &if -D 500 -W 300 -v; &mfs",
    "dch_equiv_delay_1000": "&scorr; &sweep; &dc2; &dch -e -r; &if -D 1000 -W 300 -v; &mfs",
    "dch_equiv_delay_2000": "&scorr; &sweep; &dc2; &dch -e -r; &if -D 2000 -W 300 -v; &mfs",
    "dch_equiv_delay_5000": "&scorr; &sweep; &dc2; &dch -e -r; &if -D 5000 -W 300 -v; &mfs",
    "dch_equiv_cuts_16": "&scorr; &sweep; &dc2; &dch -e -r; &if -C 16 -W 300 -v; &mfs",
    "dch_equiv_cuts_32": "&scorr; &sweep; &dc2; &dch -e -r; &if -C 32 -W 300 -v; &mfs",
    "dch_equiv_cuts_64": "&scorr; &sweep; &dc2; &dch -e -r; &if -C 64 -W 300 -v; &mfs",
    "dch_equiv_cuts_128": "&scorr; &sweep; &dc2; &dch -e -r; &if -C 128 -W 300 -v; &mfs",
    "dch_equiv_cuts_256": "&scorr; &sweep; &dc2; &dch -e -r; &if -C 256 -W 300 -v; &mfs",
    "dch_equiv_cuts_64_area": "&scorr; &sweep; &dc2; &dch -e -r; &if -a -C 64 -W 300 -v; &mfs",
    "dch_equiv_cuts_128_area": "&scorr; &sweep; &dc2; &dch -e -r; &if -a -C 128 -W 300 -v; &mfs",
    "nf_default": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4",
    "nf_cuts_8": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -C 8",
    "nf_cuts_32": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -C 32",
    "nf_flow_2": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -F 2",
    "nf_flow_8": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -F 8",
    "nf_area": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -a",
    "nf_area_c32": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -a -C 32",
    "nf_area_rounds_4": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -a -A 4",
    "nf_area_rounds_8": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -a -A 8",
    "nf_relax_10": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -R 10",
    "nf_relax_25": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -R 25",
    "nf_relax_50": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -R 50",
    "nf_edge_25": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -E 25",
    "nf_edge_50": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -E 50",
    "nf_edge_100": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -E 100",
    "nf_coarsen": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -k",
    "nf_permute": "&scorr; &sweep; &dc2; &dch -e -r; &nf -K 4 -p",
    "if2_default": "&scorr; &sweep; &dc2; &dch -e -r; &if2 -K 4",
    "if2_cuts_32": "&scorr; &sweep; &dc2; &dch -e -r; &if2 -K 4 -C 32",
    "if2_mux": "&scorr; &sweep; &dc2; &dch -e -r; &if2 -K 4 -t -m",
    "jf_default": "&scorr; &sweep; &dc2; &dch -e -r; &jf -K 4",
    "jf_cuts_16": "&scorr; &sweep; &dc2; &dch -e -r; &jf -K 4 -C 16",
    "jf_minimize": "&scorr; &sweep; &dc2; &dch -e -r; &jf -K 4 -m",
    "lf_default": "&scorr; &sweep; &dc2; &dch -e -r; &lf -K 4",
    "lf_cuts_32": "&scorr; &sweep; &dc2; &dch -e -r; &lf -K 4 -C 32",
    "lf_rounds": "&scorr; &sweep; &dc2; &dch -e -r; &lf -K 4 -C 32 -F 8 -A 8",
    "lf_relax_25": "&scorr; &sweep; &dc2; &dch -e -r; &lf -K 4 -R 25",
    "lf_cut_split": "&scorr; &sweep; &dc2; &dch -e -r; &lf -K 4 -g",
    "mf_default": "&scorr; &sweep; &dc2; &dch -e -r; &mf -K 4",
    "mf_area": "&scorr; &sweep; &dc2; &dch -e -r; &mf -K 4 -a",
    "mf_cuts_16": "&scorr; &sweep; &dc2; &dch -e -r; &mf -K 4 -C 16",
    "mf_rounds": "&scorr; &sweep; &dc2; &dch -e -r; &mf -K 4 -C 16 -F 8 -A 8",
    "of_default": "&scorr; &sweep; &dc2; &dch -e -r; &of -K 4",
    "of_cuts_32": "&scorr; &sweep; &dc2; &dch -e -r; &of -K 4 -C 32",
    "of_rounds": "&scorr; &sweep; &dc2; &dch -e -r; &of -K 4 -C 32 -F 8 -A 8",
    "of_relax_25": "&scorr; &sweep; &dc2; &dch -e -r; &of -K 4 -R 25",
    "dch_minlevel": "&scorr; &sweep; &dc2; &dch -m -r; &if -W 300 -v; &mfs",
    "dch_new_x": "&scorr; &sweep; &dc2; &dch -x -r; &if -W 300 -v; &mfs",
    "dch_new_y": "&scorr; &sweep; &dc2; &dch -y -r; &if -W 300 -v; &mfs",
    "mfs_twice": "&scorr; &sweep; &dc2; &dch -f -r; &if -W 300 -v; &mfs; &mfs",
    "no_choices": "&scorr; &sweep; &dc2; &if -W 300 -v; &mfs",
    "choices_first": "&scorr; &sweep; &dch -f -r; &dc2; &if -W 300 -v; &mfs",
}

STRUCTURES = {
    "original": [],
    "pmuxtree": ["pmuxtree"],
}


def q(path: Path) -> str:
    return '"' + path.as_posix().replace('"', '\\"') + '"'


def count_types(path: Path, top: str) -> dict[str, int]:
    doc = json.loads(path.read_text(encoding="utf-8"))
    counts: dict[str, int] = {}
    for cell in doc["modules"][top].get("cells", {}).values():
        cell_type = cell["type"]
        counts[cell_type] = counts.get(cell_type, 0) + 1
    return counts


def run_one(yosys: Path, source: Path, top: str, out: Path,
            structure_name: str, structure: list[str], abc_name: str,
            abc_script: str) -> dict:
    work = out / top / structure_name / abc_name
    work.mkdir(parents=True, exist_ok=True)
    netlist = work / "post_abc9.json"
    abc_file = work / "abc.script"
    abc_file.write_text(abc_script + "\n", encoding="utf-8")
    commands = [
        f"read_verilog {q(source)}",
        f"synth_intel -family cycloneiv -top {top} -run begin:map_ffram",
        "opt -fast -mux_undef -undriven -fine -full",
        "memory_map",
        "opt -undriven -fine",
        *structure,
        "techmap -map +/techmap.v",
        "opt -full",
        "clean -purge",
        "setundef -undriven -zero",
        f"synth_intel -family cycloneiv -top {top} -run map_ffs:map_luts",
        f"abc9 -lut 4 -W 300 -script {q(abc_file)}",
        "clean",
        "opt -fast",
        "autoname",
        "clean",
        "check",
        f"write_json {q(netlist)}",
    ]
    (work / "run.ys").write_text(";\n".join(commands) + ";\n", encoding="utf-8")
    process = subprocess.run([str(yosys), "-Q", "-p", "; ".join(commands)],
                             text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT)
    (work / "console.log").write_text(process.stdout, encoding="utf-8")
    if process.returncode or not netlist.is_file():
        return {"structure": structure_name, "abc_script": abc_name,
                "status": "FAIL", "returncode": process.returncode}
    counts = count_types(netlist, top)
    return {"structure": structure_name, "abc_script": abc_name,
            "status": "PASS", "lut": counts.get("$lut", 0),
            "dffeas": counts.get("dffeas", 0), "cell_types": counts}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--yosys", type=Path, required=True)
    parser.add_argument("--case3", type=Path, required=True)
    parser.add_argument("--case4", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.yosys = args.yosys.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)

    results = {}
    for top, source in (("test3", args.case3.resolve()),
                        ("test4", args.case4.resolve())):
        rows = []
        for structure_name, structure in STRUCTURES.items():
            for abc_name, abc_script in ABC_SCRIPTS.items():
                rows.append(run_one(args.yosys, source, top, args.out,
                                    structure_name, structure, abc_name,
                                    abc_script))
        results[top] = rows
        passed = [row for row in rows if row["status"] == "PASS"]
        if passed:
            best = min(row["lut"] for row in passed)
            names = [f"{r['structure']}+{r['abc_script']}" for r in passed
                     if r["lut"] == best]
            print(f"{top}: best={best} LUT; " + ", ".join(names))
        else:
            print(f"{top}: no passing candidate")

    (args.out / "summary.json").write_text(json.dumps({
        "schema": 1,
        "results": results,
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
