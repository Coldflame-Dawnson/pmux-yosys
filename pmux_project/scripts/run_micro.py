#!/usr/bin/env python3
"""Black-box M1 RTLIL regression. Fixture replay is only a harness test."""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIX = ROOT / "tests" / "pmux_m1" / "micro"
DEFAULT_BIN = ROOT / "official_baseline" / "toolchain" / "install" / "bin"


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def run(argv, log, cwd, env, timeout=300):
    save(log.with_suffix(log.suffix + ".command.txt"), shlex.join(map(str, argv)) + "\n")
    try:
        p = subprocess.run(list(map(str, argv)), cwd=cwd, env=env, text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        save(log, p.stdout)
        return p.returncode
    except subprocess.TimeoutExpired as e:
        save(log, (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "") + "\nTIMEOUT\n")
        return 124


def yosys_json(il, dest, yosys, work, env, label):
    script = f'read_rtlil "{il}"; hierarchy -check -top top; write_json "{dest}"'
    rc = run([yosys, "-Q", "-p", script], work / (label + ".log"), work, env)
    if rc: raise RuntimeError(f"{label}: Yosys rc={rc}")
    return json.loads(dest.read_text())["modules"]


def bit_expression(bits, module):
    index = {}
    for name, net in sorted(module["netnames"].items()):
        for pos, bit in enumerate(net["bits"]):
            if isinstance(bit, int): index.setdefault(bit, (name, pos, len(net["bits"])))
    out = []
    for bit in bits:
        if isinstance(bit, str): out.append("1'" + bit)
        else:
            name, pos, width = index[bit]
            out.append("\\" + name + (f" [{pos}]" if width > 1 else ""))
    return out[0] if len(out) == 1 else "{ " + " ".join(reversed(out)) + " }"


def replay_fixture(before, after, modules, expected, plan_path, stats_path):
    """Replay manifest bits to test verifier plumbing; it makes no inference."""
    edits = expected["expected_edits"]
    text = before.read_text()
    module = modules["top"]
    grouped = {}
    for e in edits: grouped.setdefault((e["cell"], e["port"]), {})[e["bit"]] = e["value"]
    for (cell, port), values in grouped.items():
        old = module["cells"][cell]["connections"][port]
        new = [values.get(i, x) for i, x in enumerate(old)]
        expression = bit_expression(new, module)
        pattern = rf"(  cell \$\w+ \\{re.escape(cell)}\n.*?    connect \\{port} )[^\n]+"
        text, count = re.subn(pattern, lambda m: m.group(1) + expression, text, count=1, flags=re.S)
        if count != 1: raise RuntimeError(f"fixture replay failed to locate {cell}.{port}")
    save(after, text)
    save(plan_path, json.dumps({"schema": 1, "mode": "fixture-replay", "edits": edits}, indent=2) + "\n")
    save(stats_path, json.dumps({"schema": 1, "mode": "fixture-replay", "changed_bits": len(edits),
                                "reason_codes": [] if expected["reason_code"].startswith("unspecified:") else [expected["reason_code"]]}, indent=2) + "\n")


def compare(before, after, expected, plan, active):
    issues = []
    if before.keys() != after.keys(): issues.append("module set changed")
    anticipated = {(e["module"], e["cell"], e["port"], e["bit"]): e["value"] for e in expected["expected_edits"]} if active else {}
    planned = {(e["module"], e["cell"], e["port"], e["bit"]): e["value"] for e in plan.get("edits", [])}
    if planned != anticipated: issues.append(f"plan mismatch: got {planned}, expected {anticipated}")
    expected_branches = {(e["module"],e["cell"],e["port"],e["bit"]):e.get("branch") for e in expected["expected_edits"]} if active else {}
    for e in plan.get("edits", []):
        key=(e["module"],e["cell"],e["port"],e["bit"])
        if e.get("branch") != expected_branches.get(key): issues.append(f"{key}: branch mapping mismatch")
        cell=before.get(e["module"],{}).get("cells",{}).get(e["cell"],{})
        if cell.get("type")=="$pmux" and e["port"]=="B":
            width=int(cell["parameters"]["WIDTH"],2)
            if e.get("branch") != e["bit"]//width: issues.append(f"{key}: B physical bit/branch mapping invalid")
    actual = {}
    for name in before.keys() & after.keys():
        a, b = before[name], after[name]
        if set(a.get("cells", {})) != set(b.get("cells", {})): issues.append(f"{name}: cell set changed (added={set(b.get('cells',{}))-set(a.get('cells',{}))})")
        for field in ("ports", "netnames", "memories", "attributes", "parameter_default_values"):
            if a.get(field) != b.get(field): issues.append(f"{name}: {field} changed")
        for cname in a.get("cells", {}).keys() & b.get("cells", {}).keys():
            x, y = a["cells"][cname], b["cells"][cname]
            for field in ("type", "parameters", "attributes", "port_directions"):
                if x.get(field) != y.get(field): issues.append(f"{name}.{cname}: {field} changed")
            if x["connections"].keys() != y["connections"].keys(): issues.append(f"{name}.{cname}: ports changed")
            for port in x["connections"].keys() & y["connections"].keys():
                aa, bb = x["connections"][port], y["connections"][port]
                if len(aa) != len(bb): issues.append(f"{name}.{cname}.{port}: width changed"); continue
                for i, (old, new) in enumerate(zip(aa, bb)):
                    key = (name, cname, port, i)
                    if old == new: continue
                    actual[key] = new
                    if x["type"] not in ("$mux", "$pmux") or port not in ("A", "B"):
                        issues.append(f"{key}: illegal change")
                    if key not in planned: issues.append(f"{key}: unplanned change")
                    if isinstance(old, str): issues.append(f"{key}: original constant/x/z changed")
                    if new not in ("0", "1") or planned.get(key) != new: issues.append(f"{key}: wrong replacement")
    if actual != planned: issues.append(f"actual edits {actual} differ from plan {planned}")
    return issues, len(actual)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--mode", choices=("identity", "fixture-stub", "external"), default="identity")
    p.add_argument("--dut-template", help="external command with {before},{after},{plan},{stats},{case} placeholders")
    p.add_argument("--external-kind", choices=("untriggered", "stub", "real"), default="stub")
    p.add_argument("--case", action="append", help="fixture ID, repeatable")
    p.add_argument("--bin", type=Path, default=DEFAULT_BIN)
    p.add_argument("--out", type=Path, default=ROOT / "reports" / "c-m1-micro")
    a = p.parse_args()
    a.out = a.out.resolve()
    if a.mode == "external" and not a.dut_template: p.error("external mode requires --dut-template")
    yosys, eqy = a.bin / "yosys", a.bin / "eqy"
    if not yosys.is_file() or not eqy.is_file(): p.error(f"missing Yosys/EQY in {a.bin}")
    env = os.environ.copy(); env["PATH"] = str(a.bin) + os.pathsep + env.get("PATH", "")
    a.out.mkdir(parents=True, exist_ok=True)
    expectations = json.loads((FIX / "expectations.json").read_text())["cases"]
    if a.case:
        unknown = set(a.case) - {x["id"] for x in expectations}
        if unknown: p.error(f"unknown cases: {sorted(unknown)}")
        expectations = [x for x in expectations if x["id"] in a.case]
    rows = []
    versions = {"yosys": subprocess.run([yosys, "-V"],capture_output=True,text=True).stdout.strip(),
                "eqy": subprocess.run([eqy, "--version"],capture_output=True,text=True).stdout.strip()}
    for spec in expectations:
        name = spec["id"]; work = a.out / name; work.mkdir(exist_ok=True)
        before = (FIX / spec["file"]).resolve(); after = work / "after.il"
        plan_path, stats_path = work / "plan.json", work / "stats.json"
        row = {"case": name, "mode": a.mode, "trigger_expected": spec["trigger"],
               "structure": "NOT_RUN", "yosys_check": "NOT_RUN", "eqy": "NOT_RUN",
               "semantic": "SKIP", "lut_benefit": "NOT_RUN", "issues": []}
        try:
            original = yosys_json(before, work / "before.json", yosys, work, env, "before_json")
            if a.mode == "identity":
                shutil.copyfile(before, after)
                save(plan_path, '{"schema":1,"mode":"identity","edits":[]}\n')
                save(stats_path, '{"schema":1,"mode":"identity","changed_bits":0,"reason_codes":[]}\n')
            elif a.mode == "fixture-stub": replay_fixture(before, after, original, spec, plan_path, stats_path)
            else:
                vars = {"before": str(before), "after": str(after), "plan": str(plan_path),
                        "stats": str(stats_path), "case": name, "project": str(ROOT)}
                cmd = [part.format(**vars) for part in shlex.split(a.dut_template)]
                rc = run(cmd, work / "dut.log", work, env)
                if rc: raise RuntimeError(f"DUT rc={rc}")
            if not all(x.is_file() for x in (after,plan_path,stats_path)):
                raise RuntimeError("DUT did not produce after.il, plan.json, stats.json")
            changed = yosys_json(after, work / "after.json", yosys, work, env, "after_json")
            plan = json.loads(plan_path.read_text()); stats = json.loads(stats_path.read_text())
            active = a.mode != "identity" and a.external_kind != "untriggered"
            issues, count = compare(original, changed, spec, plan, active)
            if stats.get("changed_bits") != count: issues.append("Pass changed_bits statistic differs from structural diff")
            if active and spec["reason_code"] in ("fact_conflict", "rewrite_conflict"):
                if spec["reason_code"] not in stats.get("reason_codes", []): issues.append("missing frozen reason code")
            if spec["reason_code"].startswith("unspecified:"):
                row["reason_code_contract"] = "SKIP: B must freeze exact code"
            row["structure"] = "FAIL" if issues else "PASS"
            row["issues"].extend(issues)
            check_script = f'read_rtlil "{after}"; hierarchy -check -top top; check'
            rc = run([yosys,"-Q","-p",check_script],work/"check.log",work,env)
            check_log = (work/"check.log").read_text()
            row["yosys_check"] = "PASS" if rc == 0 and "0 problems" in check_log else "FAIL"
            # EQY must compare observable ports, not same-named internal cells
            # whose outputs may disappear after a legal guarded replacement.
            internal = sorted(set(original["top"]["cells"]) |
                              (set(original["top"]["netnames"]) - set(original["top"]["ports"])))
            match_rules = "".join(f"gold-nomatch {n}\ngate-nomatch {n}\n" for n in internal)
            config = ("[gold]\nread_rtlil " + str(before) + "\n[gate]\nread_rtlil " + str(after) +
                      "\n[script]\nhierarchy -check -top top\nprep\n[match top]\n" + match_rules +
                      "[collect top]\ngroup *\nbind *\n[strategy prove]\nuse sby\nengine abc pdr\noption aigsmt none\n")
            cfg = work / "verify.eqy"; save(cfg,config)
            eqy_work = work / "eqy_work"
            rc = run([eqy,"-f","-d",eqy_work,cfg],work/"eqy.log",work,env,timeout=180)
            eqy_log = (work/"eqy.log").read_text()
            row["eqy"] = "PASS" if rc == 0 and "DONE (PASS" in eqy_log else "FAIL"
            if a.mode in ("identity", "fixture-stub"): row["semantic"] = "SKIP" # neither executes B/A
            else: row["semantic"] = ("PASS" if row["structure"] == "PASS" else "FAIL") if a.external_kind == "real" else "SKIP"
        except Exception as e:
            row["issues"].append(str(e)); row["structure"] = "FAIL"
        row["result"] = "FAIL" if any(row[k] == "FAIL" for k in ("structure","yosys_check","eqy","semantic")) else ("SKIP" if row["semantic"] == "SKIP" else "PASS")
        rows.append(row)
        print(f"{name}: {row['result']} structure={row['structure']} check={row['yosys_check']} eqy={row['eqy']} semantic={row['semantic']}",flush=True)
    summary = {"schema":1,"mode":a.mode,"versions":versions,"rows":rows,
               "counts":{s:sum(r["result"]==s for r in rows) for s in ("PASS","FAIL","SKIP")},
               "structure_correct": "PASS" if all(r["structure"]=="PASS" for r in rows) else "FAIL",
               "eqy_pass": "PASS" if all(r["eqy"]=="PASS" for r in rows) else "FAIL",
               "lut_benefit": "NOT_RUN"}
    save(a.out/"summary.json",json.dumps(summary,ensure_ascii=False,indent=2)+"\n")
    lines=[f"# M1 微型回归 ({a.mode})","",f"结构：{summary['structure_correct']}；EQY：{summary['eqy_pass']}；LUT 收益：NOT_RUN。","",
           "Fixture replay 仅验证测试框架，不能视作真实证明引擎通过。","",
           "| Case | 结构 | check | EQY | 语义 |","|---|---|---|---|---|"]
    lines += [f"| {r['case']} | {r['structure']} | {r['yosys_check']} | {r['eqy']} | {r['semantic']} |" for r in rows]
    save(a.out/"summary.md","\n".join(lines)+"\n")
    return 1 if summary["counts"]["FAIL"] else 0


if __name__ == "__main__": sys.exit(main())
