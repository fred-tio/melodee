#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Melodee in the browser: the firmware's sources as the host tests build them (web/emu/melodee_web.c), compiled
# to WebAssembly with Emscripten. After Felucca's web/emu/build.sh, itself after X0X's (charlesvestal/fm1-x0x, GPL-3.0).
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, melodee.wasm}
#   python3 -m http.server 8000 --directory build/emu, then http://localhost:8000/
# Run after the generators (./build.sh, or the host-only steps in spec/docs/local-development.md): build/gen.
# -ffp-contract=off: no fused multiply-adds, as the host build. EMU_OPT replaces -O2, EMU_CFLAGS adds flags.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/melodee_tables.h ] && [ -f build/gen/ui_fonts.h ] && [ -f build/gen/x0x_drum_tables.h ] ||
    { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (brew install emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define MELODEE_VERSION "\([^"]*\)".*/\1/p' firmware/src/melodee.c)
mkdir -p build/emu
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DMELODEE_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/melodee.wasm web/emu/melodee_web.c
sed -e "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu/melodee.wasm ($(wc -c < build/emu/melodee.wasm | tr -d ' ') B, $VERSION)"
