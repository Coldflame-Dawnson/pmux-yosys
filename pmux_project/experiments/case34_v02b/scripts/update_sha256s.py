#!/usr/bin/env python3
"""Write deterministic SHA256SUMS for the case34_v02b evidence package."""

import hashlib
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest().upper()


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    output = root / "SHA256SUMS"
    paths = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path != output
    )
    output.write_text(
        "".join(f"{digest(path)}  {path.relative_to(root).as_posix()}\n" for path in paths),
        encoding="utf-8",
        newline="\n",
    )


if __name__ == "__main__":
    main()
