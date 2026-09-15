#!/bin/bash
# Builds the core, HTTP, and tunnel tests against the Linux platform layer.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/tests-linux}"
mkdir -p "$OUT/obj"
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -pthread $(pkg-config --cflags gdk-pixbuf-2.0 libcurl)"
LIBS="$(pkg-config --libs gdk-pixbuf-2.0 libcurl) -pthread"
for f in src/core/qr.cpp src/core/deflate.cpp src/core/bundle.cpp src/core/http.cpp src/core/tunnel.cpp src/linux/platform_linux.cpp; do
    $CXX $FLAGS -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o"
done
for t in core_test server_test tunnel_test; do
    $CXX $FLAGS "tests/$t.cpp" "$OUT"/obj/*.o $LIBS -o "$OUT/$t"
done
echo "Tests built in $OUT"
