#!/usr/bin/env bash
# Cross-compiles LoopDesk.exe (Windows x64) from any OS using the Zig toolchain.
#   pip install ziglang     # one-time
#   ./build.sh
set -euo pipefail
cd "$(dirname "$0")"

if command -v zig >/dev/null 2>&1; then ZIG=(zig); else ZIG=(python3 -m ziglang); fi

[ -f res/app.ico ] || python3 res/make_icon.py
mkdir -p release

"${ZIG[@]}" c++ -target x86_64-windows-gnu -std=c++17 -O2 -s \
  -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter \
  src/main.cpp res/app.rc -o release/LoopDesk.exe \
  -static -Wl,--subsystem,windows \
  -luser32 -lgdi32 -lshell32 -lole32 -lcomdlg32 -lcomctl32 -ladvapi32 -lwinmm -luuid

ls -l release/LoopDesk.exe
