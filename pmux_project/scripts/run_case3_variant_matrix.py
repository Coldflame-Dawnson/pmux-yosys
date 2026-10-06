#!/usr/bin/env python3
"""Reproducible case3 V2 checkpoint matrix; never loads or edits production Pass."""

import argparse
import json
import os
import re
import shutil
import subprocess
import tarfile
from pathlib import Path
from types import SimpleNamespace

from diagnose_official_stages import hash_file, load_b_entry, metrics
from run_micro import ROOT, run, save
from run_official import HASHES, config

TOP = "test3"
NAMES = ("baseline", "V2-A", "V2-B", "V2-C", "V2-A+B", "V2-A+B+C")
COMPONENTS = {
    "V2-A": frozenset("A"),
    "V2-B": frozenset("B"),
    "V2-C": frozenset("C"),
    "V2-A+B": frozenset(("A", "B")),
    "V2-A+B+C": frozenset(("A", "B", "C")),
}
STAGES = ("check", "common_techmap", "pre_abc9", "post_abc9")
EXPECTED_BASELINE = {"lut": 13, "dff": 17, "io": 0}


def quote(path):
    return '"' + str(path).replace("\\", "/").replace('"', '\\"') + '"'


def commands_text(commands):
    return ";\n".join(commands) + ";\n"


def flow_commands(b_entry):
    placeholder = SimpleNamespace(
        source=Path("/case3-original/test3.v"), top=TOP, family="cycloneiv",
        stage="post_abc9", plan=Path("/unused.plan.json"),
        stats=Path("/unused.stats.json"), json=Path("/unused.json"),
        rtlil=Path("/unused.il"),
    )
    commands = b_entry.build_script(placeholder).strip().removesuffix(";").split(";\n")
    fixed = (
        "opt -fast -mux_undef -undriven -fine -full",
        "memory_map", "opt -undriven -fine", "opt_clean",
        "techmap -map +/techmap.v", "opt -full", "clean -purge",
        "setundef -undriven -zero",
    )
    if any(commands.count(item) != 1 for item in fixed):
        raise RuntimeError("B audited command list changed")
    pass_positions = [i for i, command in enumerate(commands)
                      if command.startswith("pmux_opt -write-plan ")]
    if len(pass_positions) != 1:
        raise RuntimeError("B audited entry lacks a single PMUX insertion")
    indices = [commands.index(fixed[0]), commands.index(fixed[1]),
               commands.index(fixed[2]), pass_positions[0]]
    indices += [commands.index(item) for item in fixed[3:]]
    if indices != sorted(indices):
        raise RuntimeError("B audited command order changed")
    pre = commands[:pass_positions[0]]
    post = commands[pass_positions[0] + 1:]
    if post[:5] != list(fixed[3:]):
        raise RuntimeError("B audited post-PMUX flow changed")
    return pre, post[:5]


def run_yosys(yosys, script, work, env, label):
    path = work / (label + ".ys")
    log = work / (label + ".log")
    save(path, script)
    rc = run([yosys, "-l", log, "-s", path], work / (label + ".console.log"),
             work, env, timeout=900)
    if rc:
        raise RuntimeError(f"{label}: Yosys rc={rc}; see {log}")
    if "Executing PMUX_OPT guarded analysis and atomic rewrite." in log.read_text(
        encoding="utf-8", errors="replace"
    ):
        raise RuntimeError(f"{label}: default production Pass ran during isolated experiment")
    return log


def snapshot(module_path, top=TOP):
    data = json.loads(module_path.read_text(encoding="utf-8"))
    if top not in data.get("modules", {}):
        raise RuntimeError(f"{module_path}: missing module {top}")
    return data["modules"][top]


def selected_top_only(path):
    names = re.findall(r"^module\s+\\(\S+)", path.read_text(encoding="utf-8"), re.M)
    if names != [TOP]:
        raise RuntimeError(f"{path}: expected one selected RTLIL module named {TOP}, got {names}")


def prepare_baseline(src, prefix, yosys, out, env):
    work = out / "baseline"
    work.mkdir(parents=True, exist_ok=True)
    checkpoint = work / "input_checkpoint.il"
    commands = [f"read_verilog {quote(src)}"] + prefix[1:]
    commands += [f"select {TOP}", f"write_rtlil -selected {quote(checkpoint)}"]
    run_yosys(yosys, commands_text(commands), work, env, "prepare_checkpoint")
    selected_top_only(checkpoint)
    return checkpoint


def stage_script(src, checkpoint, prefix, suffix, stage, work):
    commands = [f"read_verilog {quote(src)}"] + prefix[1:]
    commands += [f"delete {TOP}", f"read_rtlil {quote(checkpoint)}",
                 f"hierarchy -check -top {TOP}", "check -assert -noinit"]
    if stage != "check":
        commands += suffix[:2]
    if stage in ("pre_abc9", "post_abc9"):
        commands += suffix[2:5]
        end = "map_luts" if stage == "pre_abc9" else "map_cells"
        commands += [f"synth_intel -family cycloneiv -top {TOP} -run map_ffs:{end}"]
    if stage == "post_abc9":
        commands += ["hierarchy -check", "check -assert -noinit", "stat"]
    commands += [f"write_json {quote(work / (stage + '.json'))}",
                 f"select {TOP}",
                 f"write_rtlil -selected {quote(work / (stage + '.il'))}"]
    return commands_text(commands)


def run_eqy(eqy, src, gate, work, env):
    attempts = []
    for method, whole in (("default", False), ("whole", True)):
        cfg = work / (method + ".eqy")
        save(cfg, config(src, gate, TOP, whole))
        log = work / (method + ".eqy.log")
        rc = run([eqy, "-f", "-d", work / ("eqy_" + method), cfg],
                 log, work, env, timeout=900)
        passed = rc == 0 and "DONE (PASS" in log.read_text(encoding="utf-8", errors="replace")
        attempts.append({"method": method, "status": "PASS" if passed else "FAIL",
                         "rc": rc, "log": str(log), "log_sha256": hash_file(log)})
        if passed:
            return "PASS", method, attempts
    return "FAIL", None, attempts


def evaluate(name, checkpoint, src, prefix, suffix, yosys, eqy, out, env, mode):
    work = out / name
    work.mkdir(parents=True, exist_ok=True)
    row = {"variant": name, "mode": mode, "checkpoint": str(checkpoint),
           "checkpoint_sha256": hash_file(checkpoint), "check": "NOT_RUN",
           "eqy": "NOT_RUN", "eqy_method": None, "stages": {},
           "lut": None, "dff": None, "io": None, "delta_lut": None,
           "verdict": "NOT_RUN", "issues": []}
    for stage in STAGES:
        try:
            script = stage_script(src, checkpoint, prefix, suffix, stage, work)
            run_yosys(yosys, script, work, env, stage)
            net = work / (stage + ".json")
            il = work / (stage + ".il")
            info = {
                "rtlil": str(il), "rtlil_sha256": hash_file(il),
                "json": str(net), "json_sha256": hash_file(net),
                "script": str(work / (stage + ".ys")),
                "script_sha256": hash_file(work / (stage + ".ys")),
                "log": str(work / (stage + ".log")),
                "log_sha256": hash_file(work / (stage + ".log")),
                "metrics": metrics(snapshot(net)),
            }
            row["stages"][stage] = info
            if stage == "check":
                row["check"] = "PASS"
        except Exception as exc:
            row["issues"].append(str(exc))
            row["check"] = "FAIL" if stage == "check" else row["check"]
            row["verdict"] = "REJECT_CHECK" if stage == "check" else "REJECT_SYNTH"
            return row
    final = row["stages"]["post_abc9"]["metrics"]
    row.update(lut=final["lut"], dff=final["dff"], io=final["io"])
    row["eqy"], row["eqy_method"], row["eqy_attempts"] = run_eqy(
        eqy, src, work / "post_abc9.il", work, env)
    return row


def adjudicate(rows, mode):
    baseline = rows[0]
    if baseline["check"] != "PASS" or baseline["eqy"] != "PASS":
        raise RuntimeError("baseline check/EQY failed; matrix cannot be judged")
    actual = {key: baseline[key] for key in EXPECTED_BASELINE}
    if actual != EXPECTED_BASELINE:
        raise RuntimeError(f"checkpoint baseline differs from official resources: {actual}")
    baseline["verdict"] = "REFERENCE"
    for row in rows[1:]:
        if row["verdict"].startswith("REJECT") or (
            row["verdict"] == "NOT_RUN" and row["eqy"] == "NOT_RUN"
        ):
            continue
        row["delta_lut"] = row["lut"] - baseline["lut"]
        row["delta_dff"] = row["dff"] - baseline["dff"]
        row["delta_io"] = row["io"] - baseline["io"]
        if row["eqy"] != "PASS":
            row["verdict"] = "REJECT_EQY"
        elif row["delta_dff"] > 0 or row["delta_io"] > 0:
            row["verdict"] = "REJECT_RESOURCES"
        elif mode == "HARNESS_SMOKE":
            row["verdict"] = "HARNESS_ONLY"
        elif row["delta_lut"] >= 0:
            row["verdict"] = "NO_GAIN"
        else:
            row["verdict"] = "GAIN"
    gained = [r["variant"] for r in rows[1:] if r["verdict"] == "GAIN"]
    minimal = [name for name in gained if not any(
        COMPONENTS[other] < COMPONENTS[name] for other in gained)]
    return minimal


def render_markdown(report):
    lines = [
        "# case3 V2 实验矩阵", "",
        f"模式：{report['mode']}；官方输入 SHA-256：{report['official_input_sha256']}。",
        "HARNESS_SMOKE 只验证运行器，不代表 B 变体或真实 LUT 收益。", "",
        "| 变体 | Check | EQY | ABC9前组合cell | LUT | ΔLUT | DFF/IO | 判定 |",
        "|---|---|---|---:|---:|---:|---|---|",
    ]
    for row in report["rows"]:
        pre = row["stages"].get("pre_abc9", {}).get("metrics", {})
        dff_io = (f"{row['dff']}/{row['io']}" if row["dff"] is not None else "—")
        lines.append(f"| {row['variant']} | {row['check']} | {row['eqy']} | "
                     f"{pre.get('total_comb', '—')} | {row['lut'] if row['lut'] is not None else '—'} | "
                     f"{row['delta_lut'] if row['delta_lut'] is not None else '—'} | "
                     f"{dff_io} | {row['verdict']} |")
    lines += ["", "ABC9 前 AND/OR/NOT/MUX 及各阶段完整 cell 统计见 summary.json；"
              "所有 RTLIL、JSON、脚本、命令和日志见 evidence.tar.gz。", ""]
    lines += ["| 变体 | common techmap后组合cell | ABC9前AND | OR | NOT | MUX |",
              "|---|---:|---:|---:|---:|---:|"]
    for row in report["rows"]:
        common = row["stages"].get("common_techmap", {}).get("metrics", {})
        pre = row["stages"].get("pre_abc9", {}).get("metrics", {})
        lines.append(f"| {row['variant']} | {common.get('total_comb', '—')} | "
                     f"{pre.get('and', '—')} | {pre.get('or', '—')} | "
                     f"{pre.get('not', '—')} | {pre.get('basic_mux', '—')} |")
    lines.append("")
    if report["mode"] == "REAL_VARIANTS" and not report["missing_variants"]:
        if report["minimal_gain_variants"]:
            lines.append("产生收益的最小变体：" + "、".join(report["minimal_gain_variants"]) + "。")
        elif all(row["lut"] == 13 for row in report["rows"][1:]):
            lines.append("五个变体均未减少 ABC9 后 LUT；当前三类规则不值得进入生产实现。")
        else:
            lines.append("无通过验收且减少 ABC9 后 LUT 的变体。")
    else:
        lines.append("B 的五个真实变体尚未完成本矩阵；收益结论为 NOT_RUN。")
    return "\n".join(lines) + "\n"


def archive_evidence(out):
    excluded = {"evidence.tar.gz", "SHA256SUMS.txt"}
    paths = sorted(path for path in out.rglob("*")
                   if path.is_file() and path.name not in excluded)
    digest_lines = [f"{hash_file(path)}  {path.relative_to(out).as_posix()}"
                    for path in paths]
    save(out / "SHA256SUMS.txt", "\n".join(digest_lines) + "\n")
    with tarfile.open(out / "evidence.tar.gz", "w:gz") as archive:
        for path in paths + [out / "SHA256SUMS.txt"]:
            archive.add(path, arcname=path.relative_to(out).as_posix())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=ROOT / "tests" / "case3_variants" / "manifest.json")
    parser.add_argument("--out", type=Path, default=ROOT / "reports" / "c-case3-v2-matrix")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--baseline-only", action="store_true")
    group.add_argument("--smoke-with-baseline", action="store_true")
    args = parser.parse_args()
    args.bin = args.bin.resolve()
    args.source = args.source.resolve()
    args.out = args.out.resolve()
    args.manifest = args.manifest.resolve()
    yosys, eqy = args.bin / "yosys", args.bin / "eqy"
    if not yosys.is_file() or not eqy.is_file():
        parser.error("frozen Yosys/EQY missing")
    if hash_file(args.source) != HASHES[2]:
        parser.error("original official case3 RTL SHA-256 mismatch")
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("checkpoint") != "post_opt_undriven_before_opt_clean":
        parser.error("unexpected case3 variant manifest schema/checkpoint")
    if set(manifest.get("variants", {})) != set(NAMES[1:]):
        parser.error("manifest must list exactly five fixed variants")
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("output directory is not empty; use a fresh evidence directory")
    args.out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.manifest, args.out / "input_manifest.json")
    env = os.environ.copy()
    env["PATH"] = str(args.bin) + os.pathsep + env.get("PATH", "")
    prefix, suffix = flow_commands(load_b_entry())
    baseline_checkpoint = prepare_baseline(args.source, prefix, yosys, args.out, env)
    rows = [evaluate("baseline", baseline_checkpoint, args.source, prefix, suffix,
                     yosys, eqy, args.out, env, "OFFICIAL_BASELINE")]
    if rows[0]["check"] != "PASS" or rows[0]["eqy"] != "PASS" or any(
        rows[0][key] != value for key, value in EXPECTED_BASELINE.items()
    ):
        raise RuntimeError(f"baseline calibration failed: {rows[0]}")
    mode = "HARNESS_SMOKE" if args.smoke_with_baseline else (
        "BASELINE_ONLY" if args.baseline_only else "REAL_VARIANTS")
    missing = []
    for name in NAMES[1:]:
        spec = manifest["variants"][name]
        if args.baseline_only:
            rows.append({"variant": name, "mode": "NOT_RUN", "check": "NOT_RUN",
                         "eqy": "NOT_RUN", "eqy_method": None, "stages": {},
                         "lut": None, "dff": None, "io": None, "delta_lut": None,
                         "verdict": "NOT_RUN", "issues": []})
            missing.append(name)
            continue
        if args.smoke_with_baseline:
            checkpoint = baseline_checkpoint
        elif spec is None:
            missing.append(name)
            rows.append({"variant": name, "mode": "NOT_RUN", "check": "NOT_RUN",
                         "eqy": "NOT_RUN", "eqy_method": None, "stages": {},
                         "lut": None, "dff": None, "io": None, "delta_lut": None,
                         "verdict": "NOT_RUN", "issues": ["B checkpoint not supplied"]})
            continue
        else:
            try:
                path = Path(spec["rtlil"])
                checkpoint = (args.manifest.parent / path).resolve() if not path.is_absolute() else path
                if not checkpoint.is_file() or hash_file(checkpoint) != spec["sha256"]:
                    raise RuntimeError(f"{name}: checkpoint missing or SHA-256 mismatch")
                selected_top_only(checkpoint)
                copied = args.out / name / "input_checkpoint.il"
                copied.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(checkpoint, copied)
                checkpoint = copied
            except Exception as exc:
                rows.append({"variant": name, "mode": mode, "check": "NOT_RUN",
                             "eqy": "NOT_RUN", "eqy_method": None, "stages": {},
                             "lut": None, "dff": None, "io": None, "delta_lut": None,
                             "verdict": "REJECT_INPUT", "issues": [str(exc)]})
                continue
        row = evaluate(name, checkpoint, args.source, prefix, suffix,
                       yosys, eqy, args.out, env, mode)
        rows.append(row)
        print(f"{name}: check={row['check']} EQY={row['eqy']} LUT={row['lut']}", flush=True)
    minimal = adjudicate(rows, mode)
    report = {
        "schema": 1, "mode": mode, "branch_base": "80c8096f379583d1552b7d845bd7be75b7ea2a9c",
        "official_source": str(args.source), "official_input_sha256": hash_file(args.source),
        "manifest_sha256": hash_file(args.manifest),
        "b_audited_entry_sha256": hash_file(ROOT / "pass" / "run_synth_intel_pmux.py"),
        "official_baseline_resources": EXPECTED_BASELINE,
        "yosys_version": subprocess.run([yosys, "-V"], capture_output=True, text=True).stdout.strip(),
        "eqy_version": subprocess.run([eqy, "--version"], capture_output=True, text=True).stdout.strip(),
        "rows": rows, "missing_variants": missing, "minimal_gain_variants": minimal,
        "real_lut_benefit": "NOT_RUN" if mode != "REAL_VARIANTS" or missing
            else "FAIL" if any(row["verdict"].startswith("REJECT") for row in rows[1:])
            else "PASS" if minimal else "NO_GAIN",
    }
    save(args.out / "summary.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    save(args.out / "summary.md", render_markdown(report))
    archive_evidence(args.out)
    print(f"matrix={report['real_lut_benefit']} missing={len(missing)} "
          f"archive={args.out / 'evidence.tar.gz'}", flush=True)
    if mode == "REAL_VARIANTS" and (missing or any(
        row["verdict"].startswith("REJECT") for row in rows[1:]
    )):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
