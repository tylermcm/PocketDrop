#!/bin/bash
# Builds core_test, server_test and tunnel_test for macOS (native architecture).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/tests}"
mkdir -p "$OUT/obj"
FLAGS="-std=c++20 -O2 -mmacosx-version-min=11.0"
for f in src/core/qr.cpp src/core/deflate.cpp src/core/bundle.cpp src/core/http.cpp src/core/tunnel.cpp; do
    clang++ $FLAGS -c "$f" -o "$OUT/obj/$(basename "$f" .cpp).o"
done
clang++ $FLAGS -fobjc-arc -c src/mac/platform_mac.mm -o "$OUT/obj/platform_mac.o"
LIBS="-framework Foundation -framework CoreGraphics -framework ImageIO -framework QuickLookThumbnailing
      -framework Security -framework SystemConfiguration -lcurl"
for t in core_test server_test tunnel_test; do
    clang++ $FLAGS "tests/$t.cpp" "$OUT"/obj/*.o $LIBS -o "$OUT/$t"
done
echo "Tests built in $OUT"
