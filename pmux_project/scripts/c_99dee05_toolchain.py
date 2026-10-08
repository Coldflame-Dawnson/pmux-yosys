#!/usr/bin/env python3
"""Role C frozen-input and executable attestation; exits before tests on mismatch."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

EXPECTED_COMMITS = {
    "yosys": "9f75ca1f9834a39a863915b5dae0c7b1e33533bc",
    "eqy": "4a72eb94fc253062464afee4d0018359017bb846",
    "sby": "b1a1e98cba941ec8433f8dc27f416cd7bb7f14be",
    "abc": "a51bf4a7bb38ce06ea3a544b1463b40832213917",
}
EXPECTED_INPUTS = {
    "test1": "2d2dba011b0e8799d0de3790b69b7f01c6eb6ac665c4321b57b0378a4972fd13",
    "test2": "e43c9b437071488551caf3121e227e86f4fe3190751f245beac3b7ecc3fdb146",
    "test3": "1551b9212622ccef6019d6e77de2c43dc8c80d267957b55a42121f34a08780e9",
    "test4": "c0c5042928bd6bf568e2b7fb2193f2b6db9c4193ba3a5378fc50a331d43422d7",
}
PROJECT = Path(__file__).resolve().parents[1]
def output(argv):
    return subprocess.check_output(argv, text=True, stderr=subprocess.STDOUT).strip()
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tool = (PROJECT / "official_baseline/toolchain/install").resolve().parent
    bin_dir = tool / "install/bin"
    sources = {
        "yosys": tool / "yosys-v0.69-src",
        "eqy": tool / "eqy-v0.69-src",
        "sby": tool / "sby-v0.69-src",
        "abc": tool / "yosys-v0.69-src/abc",
    }
    binary = {"yosys": bin_dir / "yosys", "eqy": bin_dir / "eqy",
              "sby": bin_dir / "sby", "abc": bin_dir / "yosys-abc"}
    versions = {
        "yosys": output([str(binary["yosys"]), "-V"]),
        "eqy": output([str(binary["eqy"]), "--version"]),
        "sby": output([str(binary["sby"]), "--version"]),
        "abc": output([str(binary["abc"]), "-c", "version"]),
    }
    records = {}
    for name in EXPECTED_COMMITS:
        commit = output(["git", "-C", str(sources[name]), "rev-parse", "HEAD"])
        assert commit == EXPECTED_COMMITS[name], (name, commit)
        records[name] = {"source_commit": commit, "binary": str(binary[name]),
                         "executable_sha256": sha(binary[name]), "raw_version": versions[name]}
    inputs = {}
    for top, expected in EXPECTED_INPUTS.items():
        path = (PROJECT / "pmux_case/competition_case" / (top + ".v")).resolve()
        digest = sha(path)
        assert digest == expected, (top, digest)
        inputs[top] = {"path": str(path), "sha256": digest}
    result = {"toolchain": records, "official_inputs": inputs,
              "tested_b_commit": "99dee059077e83969cede6e292dc1ea9139389e3"}
    (out / "toolchain.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = ["B commit: " + result["tested_b_commit"]]
    for name, rec in records.items():
        lines += [f"{name} source_commit: {rec['source_commit']}",
                  f"{name} executable_sha256: {rec['executable_sha256']}",
                  f"{name} raw_version: {rec['raw_version']}"]
    for top, rec in inputs.items():
        lines.append(f"{top}.v SHA-256: {rec['sha256']} {rec['path']}")
    (out / "toolchain.txt").write_text("\n".join(lines) + "\n")
    print("FROZEN_INPUTS_AND_TOOL_COMMITS_PASS")
if __name__ == "__main__":
    main()
