#!/bin/bash
# Builds the standalone Qt Quick app and checks that it starts, shows the engine's state and draws it.
#   needs cmake, Qt 6.4+ (Core Gui Qml Quick and the QtQuick.Controls/Dialogs/Shapes modules), libcurl and gdk-pixbuf
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S app -B build/app -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/app -j"$(nproc)" >/dev/null
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/files" "$work/cfg"
echo "hello" > "$work/files/note.txt"
XDG_RUNTIME_DIR="$work/rt" XDG_CONFIG_HOME="$work/cfg" XDG_DATA_HOME="$work/cfg" HOME="$work" \
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software POCKETDROP_SNAPSHOT="$work/shot.png" \
    timeout 60 build/app/PocketDrop "$work/files/note.txt"
python3 - "$work/shot.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
colors = len(set(im.resize((100, 180)).getdata()))
assert w >= 300 and h >= 500, (w, h)
assert colors > 20, f"the window looks empty ({colors} colours)"


print("standalone app drew", w, "x", h, "with", colors, "colours")
PY
