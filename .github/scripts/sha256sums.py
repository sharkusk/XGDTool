#!/usr/bin/env python3
"""Write a coreutils-compatible SHA256SUMS file for every file under a directory."""

import hashlib
import sys
from pathlib import Path


def main():
    root = Path(sys.argv[1])
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "SHA256SUMS"
    files = sorted(
        path for path in root.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    )
    lines = []
    for path in files:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{digest}  {path.relative_to(root).as_posix()}")
    text = "\n".join(lines) + ("\n" if lines else "")
    output.write_text(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        raise SystemExit(f"usage: {sys.argv[0]} DIRECTORY [SHA256SUMS]")
    main()
