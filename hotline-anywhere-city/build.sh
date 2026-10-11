#!/bin/bash
# Builds the GTA 2 plugin (32-bit) with Zig's clang. Usage: ./build.sh [probe|hac]
set -e
cd "$(dirname "$0")"
ZIG="${ZIG:-$LOCALAPPDATA/hac-tools/zig-windows-x86_64-0.13.0/zig.exe}"
T=${1:-hac}
python tools/sheets.py gen > build-preflight.txt || { cat build-preflight.txt; exit 1; }
mkdir -p build
for f in third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde32.c; do
  "$ZIG" cc -target x86-windows-gnu -O2 -c "$f" -o "build/$(basename "$f" .c).o" -Ithird_party/minhook/include
done
"$ZIG" cc -target x86-windows-gnu -O2 -w -c third_party/stb/stb_vorbis.c -o build/stb_vorbis.o
if [ "$T" = probe ]; then SRCS=src/probe.cpp; OUT=build/probe.asi; else SRCS="$(ls src/*.cpp | grep -v probe.cpp)"; OUT=build/HotlineCity.asi; fi
"$ZIG" c++ -target x86-windows-gnu -O2 -std=c++17 -shared -fno-exceptions -fno-rtti -Wno-unused-function \
  -Ithird_party/minhook/include -Isrc -Isrc/gen $SRCS build/*.o -o "$OUT" -lwinmm -luser32 -s
ls -la "$OUT"
