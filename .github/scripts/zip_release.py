#!/usr/bin/env python3
"""Zip a file or directory for a release asset.

A directory is stored under its own name, so unzipping recreates that folder.
"""

import sys
import zipfile
from pathlib import Path


def zip_release(source, dest):
    source = Path(source)
    dest = Path(dest)
    with zipfile.ZipFile(dest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        if source.is_file():
            archive.write(source, source.name)
            return
        for path in sorted(item for item in source.rglob("*") if item.is_file()):
            archive.write(path, Path(source.name) / path.relative_to(source))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} SOURCE DEST.zip")
    zip_release(sys.argv[1], sys.argv[2])
