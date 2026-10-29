#!/usr/bin/env python3
"""Copy non-system dylibs next to a macOS executable and rewrite their install names."""

import os
import subprocess
import sys


def otool_deps(path):
    output = subprocess.check_output(["otool", "-L", path], text=True)
    deps = []
    for line in output.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        deps.append(line.split(" (", 1)[0].strip())
    return deps


def is_system(dep):
    return dep.startswith(("/usr/lib/", "/System/")) or dep.startswith("@")


def bundle(executable):
    dest = os.path.dirname(os.path.abspath(executable))
    pending = [os.path.abspath(executable)]
    copied = set()

    while pending:
        current = pending.pop()
        for dep in otool_deps(current):
            if is_system(dep) or dep == current:
                continue
            base = os.path.basename(dep)
            target = os.path.join(dest, base)
            if base not in copied:
                if not os.path.exists(dep):
                    raise SystemExit(f"missing dependency: {dep}")
                subprocess.check_call(["cp", "-p", dep, target])
                os.chmod(target, 0o644)
                subprocess.check_call(["install_name_tool", "-id", f"@executable_path/{base}", target])
                copied.add(base)
                pending.append(target)
            subprocess.check_call(["install_name_tool", "-change", dep, f"@executable_path/{base}", current])

    for name in [os.path.basename(executable), *sorted(copied)]:
        subprocess.check_call(["codesign", "--force", "--sign", "-", os.path.join(dest, name)])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} EXECUTABLE")
    bundle(sys.argv[1])
