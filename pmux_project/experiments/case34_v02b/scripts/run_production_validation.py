#!/usr/bin/env python3
"""Validate the production case3/case4 rewrite at the frozen insertion point."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def run(command, log: Path, env=None, timeout=900):
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with log.open("w", encoding="utf-8") as stream:
        completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                   text=True, env=env, timeout=timeout, check=False)
    return completed.returncode, time.perf_counter() - started


def cell_counts(path: Path, top: str):
    document = json.loads(path.read_text(encoding="utf-8"))
    counts = {}
    for cell in document["modules"][top].get("cells", {}).values():
        counts[cell["type"]] = counts.get(cell["type"], 0) + 1
    return dict(sorted(counts.items()))


def eqy_config(gold: Path, gate: Path, top: str, dffeas: Path) -> str:
    return f"""[gold]
read_verilog -formal {gold}

[gate]
read_rtlil {gate}
read_verilog -formal -overwrite {dffeas}

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

[strategy pdr]
use sby
engine abc pdr
timeout 300
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    script_path = Path(__file__).resolve()
    default_repo = script_path.parents[4]
    parser.add_argument("--repo", type=Path, default=default_repo)
    parser.add_argument("--tool-bin", type=Path, required=True)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    tool_bin = args.tool_bin.resolve()
    official = args.official.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    yosys = tool_bin / "yosys"
    eqy = tool_bin / "eqy"
    plugin = repo / "pmux_project/pass/pmux_opt.so"
    runner = repo / "pmux_project/pass/run_synth_intel_pmux.py"
    abc_script = repo / "pmux_project/experiments/case34_v02b/evidence/generated/abc.script"
    dffeas = repo / "pmux_project/official_baseline/environment/dffeas_formal.v"
    required = (yosys, eqy, plugin, runner, abc_script, dffeas,
                official / "test3.v", official / "test4.v")
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        parser.error("missing required files: " + ", ".join(missing))

    jobs = (("case3", "test3", 13, 17, 11, 12),
            ("case4", "test4", 27, 10, 27, 10))
    results = {}
    for case, top, expected_lut, expected_dff, oracle_lut, oracle_dff in jobs:
        source = official / f"{top}.v"
        case_result = {"source": str(source), "source_sha256": sha256(source),
                       "oracle": {"lut": oracle_lut, "dff": oracle_dff}}
        for variant in ("baseline", "production"):
            directory = out / case / variant
            directory.mkdir(parents=True, exist_ok=True)
            command = [sys.executable, str(runner), "--yosys", str(yosys),
                       "--plugin", str(plugin), "--source", str(source),
                       "--top", top, "--stage", "post_abc9",
                       "--plan", str(directory / "plan.json"),
                       "--stats", str(directory / "stats.json"),
                       "--json", str(directory / "post_abc9.json"),
                       "--rtlil", str(directory / "post_abc9.il"),
                       "--log", str(directory / "yosys.log"),
                       "--script", str(directory / "run.ys")]
            if variant == "production":
                command.extend(("--v02-case34", "--abc-script", str(abc_script)))
            rc, elapsed = run(command, directory / "driver.log")
            record = {"returncode": rc, "wall_seconds": elapsed}
            if rc == 0:
                record["cell_counts"] = cell_counts(directory / "post_abc9.json", top)
                record["gate_sha256"] = sha256(directory / "post_abc9.il")
                record["plan"] = json.loads((directory / "plan.json").read_text(encoding="utf-8"))
                record["stats"] = json.loads((directory / "stats.json").read_text(encoding="utf-8"))
            case_result[variant] = record

        production = case_result["production"]
        counts = production.get("cell_counts", {})
        count_ok = (production["returncode"] == 0 and
                    counts.get("$lut", 0) == expected_lut and
                    counts.get("dffeas", 0) == expected_dff)
        eqy_dir = out / case / "gate_eqy"
        eqy_dir.mkdir(parents=True, exist_ok=True)
        config = eqy_dir / f"{case}.eqy"
        config.write_text(eqy_config(source, out / case / "production/post_abc9.il",
                                     top, dffeas), encoding="utf-8")
        env = os.environ.copy()
        env["PATH"] = str(tool_bin) + os.pathsep + env.get("PATH", "")
        rc, elapsed = run([str(eqy), "-f", "-d", str(eqy_dir / "work"), str(config)],
                          eqy_dir / "eqy.log", env=env)
        eqy_pass = rc == 0 and (eqy_dir / "work/PASS").is_file()
        case_result["gate_eqy"] = {"returncode": rc, "wall_seconds": elapsed,
                                           "pass": eqy_pass}
        case_result["production_count_check"] = "PASS" if count_ok else "FAIL"
        case_result["p2_status"] = ("PASS" if count_ok and eqy_pass and
                                    expected_lut == oracle_lut and expected_dff == oracle_dff
                                    else "GAP")
        results[case] = case_result

    summary = {"schema": 1, "producer": "run_production_validation.py",
               "results": results}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    failed = [case for case, result in results.items()
              if result["production_count_check"] != "PASS" or not result["gate_eqy"]["pass"]]
    print(json.dumps(summary, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
