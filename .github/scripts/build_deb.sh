#!/bin/bash
# Build a .deb that installs XGDTool into /usr/bin and uses the distro libraries.
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 BINARY OUTPUT_DIR" >&2
    exit 2
fi

binary=$1
output_dir=$2
version="$("$binary" --version | tr -d '[:space:]')"
arch="$(dpkg --print-architecture)"
name="xgdtool_${version}-1_${arch}"
root="$(mktemp -d)"

mkdir -p "$root/DEBIAN" "$root/usr/bin"
install -m 0755 "$binary" "$root/usr/bin/XGDTool"

cat > "$root/DEBIAN/control" <<EOF
Package: xgdtool
Version: ${version}-1
Section: utils
Priority: optional
Architecture: ${arch}
Depends: libc6 (>= 2.35), libgcc-s1, libstdc++6, liblz4-1, libzstd1, libssl3 | libssl3t64, libcurl4 | libcurl4t64
Maintainer: sharkusk <https://github.com/sharkusk/XGDTool>
Homepage: https://github.com/sharkusk/XGDTool
Description: Convert original Xbox and Xbox 360 disc images
 Command-line tool for ISO, extracted files, GoD, CCI, CSO, and ZAR.
EOF

mkdir -p "$output_dir"
dpkg-deb --root-owner-group --build "$root" "$output_dir/${name}.deb"
rm -rf "$root"
