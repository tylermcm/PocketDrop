#!/bin/bash
# Builds an x86_64 Linux executable and tarball on Ubuntu 22.04 or newer.
# Requires: g++, pkg-config, libgtk-3-dev, libcurl4-openssl-dev.
set -euo pipefail
cd "$(dirname "$0")"

OBJ=build/linux-obj
OUT=build/PocketDrop-linux-x86_64
rm -rf "$OBJ" "$OUT" build/PocketDrop-linux-x86_64.tar.gz
mkdir -p "$OBJ" "$OUT"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -pthread $(pkg-config --cflags gtk+-3.0 libcurl)"
LIBS="$(pkg-config --libs gtk+-3.0 libcurl) -pthread"
SOURCES="src/core/qr.cpp src/core/deflate.cpp src/core/bundle.cpp src/core/http.cpp src/core/tunnel.cpp
         src/ui/ui.cpp src/ui/icons.cpp src/linux/platform_linux.cpp src/linux/gfx_cairo.cpp src/linux/main_linux.cpp"

objs=()
for f in $SOURCES; do
    o="$OBJ/$(basename "${f%.*}").o"
    $CXX $CXXFLAGS -c "$f" -o "$o"
    objs+=("$o")
done
$CXX -o "$OUT/PocketDrop" "${objs[@]}" $LIBS
strip "$OUT/PocketDrop"
cp src/linux/app.png "$OUT/PocketDrop.png"
cp src/linux/README.txt "$OUT/README.txt"
tar -C build -czf build/PocketDrop-linux-x86_64.tar.gz PocketDrop-linux-x86_64
echo "Built $OUT/PocketDrop and build/PocketDrop-linux-x86_64.tar.gz"
