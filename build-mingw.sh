#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build/mingw-tests
cmake -S . -B build/mingw-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build/mingw-tests
ctest --test-dir build/mingw-tests --output-on-failure

mkdir -p dist/mingw-x64
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -w -shared -static -static-libgcc -static-libstdc++ \
  -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0601 \
  -Isrc -Ithird_party src/magpie_plugin.cpp src/http.cpp src/MagpieRoute.def \
  -o dist/mingw-x64/MagpieRoute.dll -lwinhttp -lshell32
