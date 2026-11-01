#!/usr/bin/env python3
"""Package B default-selection evidence, excluding tools and official RTL."""

import argparse
import hashlib
import json
import os
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    reports = ROOT / "pmux_project/reports"
    tool_bin = ROOT / "pmux_project/official_baseline/toolchain/install/bin"
    env = os.environ.copy()
    env["PATH"] = str(tool_bin) + os.pathsep + env.get("PATH", "")
    metadata = {}
    for name, command in {
        "revision": ["git", "rev-parse", "HEAD"],
        "submodules": ["git", "submodule", "status"],
        "yosys_version": [tool_bin / "yosys", "-V"],
        "eqy_version": [tool_bin / "eqy", "--version"],
        "sby_version": [tool_bin / "sby", "--version"],
        "abc_version": [tool_bin / "yosys-abc", "-c", "version"],
    }.items():
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=60)
        metadata[name] = {"returncode": result.returncode, "output": result.stdout}
    metadata["binary_sha256"] = {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [*(tool_bin / name for name in ("yosys", "yosys-abc", "eqy", "sby")),
                     ROOT / "pmux_project/pass/pmux_opt.so"]
    }
    (out / "toolchain.json").write_text(json.dumps(metadata, indent=2) + "\n")
    official = ROOT / "pmux_project/pmux_case/competition_case"
    forbidden = {hashlib.sha256((official / f"test{i}.v").read_bytes()).hexdigest()
                 for i in range(1, 5)}
    sources = {
        "default": reports / "b-case12-default-selection-final-delivery",
        "micro19": reports / "b-case12-default-micro19",
        "random100": reports / "b-case12-default-random100",
        "profile14": reports / "b-case12-default-profile14",
        "initial_formal_config_failure": reports / "b-case12-default-selection",
    }
    archive = out / "b_default_selection_evidence.tar.gz"
    manifest = []
    with tarfile.open(archive, "w:gz") as package:
        for label, source in sources.items():
            if not source.is_dir():
                raise SystemExit(f"MISSING {source}")
            for path in sorted(source.rglob("*")):
                if not path.is_file() or path.is_symlink():
                    continue
                relative = path.relative_to(source)
                if any(part in ("runtime", "build", "__pycache__") for part in relative.parts):
                    continue
                if path.suffix in (".so", ".pyc"):
                    continue
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                if digest in forbidden:
                    raise SystemExit(f"refusing to archive official RTL: {path}")
                name = f"{label}/{relative.as_posix()}"
                package.add(path, arcname=name, recursive=False)
                manifest.append({"path": name, "sha256": digest, "bytes": path.stat().st_size})
    (out / "MANIFEST.json").write_text(json.dumps({
        "scope": "B implementation evidence; A must independently regenerate gates",
        "official_rtl_included": False, "tool_binaries_included": False,
        "files": manifest,
    }, indent=2) + "\n")
    paths = (archive, out / "MANIFEST.json", out / "toolchain.json")
    (out / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in paths))
    print(f"PACKAGED {len(manifest)} files, {archive.stat().st_size} bytes")


if __name__ == "__main__":
    main()
