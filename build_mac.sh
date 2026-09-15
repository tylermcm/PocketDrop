#!/bin/bash
# Builds build/PocketDrop.app (universal arm64 + x86_64, ad-hoc signed) and build/PocketDrop-mac.zip.
# Requires Xcode or the Command Line Tools (xcode-select --install).
set -euo pipefail
cd "$(dirname "$0")"

ARCHS=${ARCHS:-"-arch arm64 -arch x86_64"}
MIN=11.0
OBJ=build/mac-obj
APP=build/PocketDrop.app
rm -rf "$OBJ" "$APP" build/PocketDrop-mac.zip
mkdir -p "$OBJ" "$APP/Contents/MacOS" "$APP/Contents/Resources"

CXXFLAGS="-std=c++20 -O2 -Wall -Wno-unused-parameter $ARCHS -mmacosx-version-min=$MIN"
SOURCES="src/core/qr.cpp src/core/deflate.cpp src/core/bundle.cpp src/core/http.cpp src/core/tunnel.cpp
         src/ui/ui.cpp src/ui/icons.cpp src/mac/platform_mac.mm src/mac/gfx_cg.mm src/mac/main_mac.mm"

pids=()
objs=()
for f in $SOURCES; do
    name=$(basename "$f")
    o="$OBJ/${name%.*}.o"
    flags="$CXXFLAGS"
    [[ "$f" == *.mm ]] && flags="$flags -fobjc-arc"
    clang++ $flags -c "$f" -o "$o" &
    pids+=($!)
    objs+=("$o")
done
for p in "${pids[@]}"; do wait "$p"; done

clang++ $ARCHS -mmacosx-version-min=$MIN -o "$APP/Contents/MacOS/PocketDrop" "${objs[@]}" \
    -framework Cocoa -framework CoreText -framework ImageIO -framework QuickLookThumbnailing \
    -framework Security -framework SystemConfiguration -lcurl
strip -x "$APP/Contents/MacOS/PocketDrop"

cp src/mac/Info.plist "$APP/Contents/Info.plist"
cp src/mac/app.icns "$APP/Contents/Resources/app.icns"
codesign --force --sign - --timestamp=none "$APP"
ditto -c -k --keepParent "$APP" build/PocketDrop-mac.zip
echo "Built $APP ($(du -sh "$APP" | cut -f1))"
