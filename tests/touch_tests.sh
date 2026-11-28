#!/usr/bin/env bash
set -euo pipefail
# Use the same pinned LovyanGFX headers as PlatformIO. SDL headers are required
# for its type declarations; the control fixture needs no window or audio.
cd "$(dirname "$0")/.."
: "${CXX:=g++}"
: "${LGFX_SOURCE:=.pio/libdeps/native-cyd/LovyanGFX/src}"
: "${TOUCH_BUILD_DIR:=.pio/touch-tests}"
if [[ ! -f "$LGFX_SOURCE/LovyanGFX.hpp" ]]; then
    printf '%s\n' 'Set LGFX_SOURCE to the pinned LovyanGFX src directory (or build native-cyd first).' >&2
    exit 2
fi
mkdir -p "$TOUCH_BUILD_DIR"
extra=()
if command -v sdl2-config >/dev/null; then
    read -r -a extra <<< "$(sdl2-config --cflags)"
fi
if [[ -n "${SDL_INCLUDE:-}" ]]; then extra+=("-I$SDL_INCLUDE"); fi
san=()
if [[ "${SANITIZE:-}" == 1 ]]; then san=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$CXX" -std=c++17 -O1 -g "${san[@]}" tests/touch_viewport.cpp -o "$TOUCH_BUILD_DIR/viewport"
"$CXX" -std=c++17 -O1 -g -ffunction-sections -fdata-sections "${san[@]}" \
    -DGRIM_NATIVE=1 -DGRIM_CYD=1 -DLGFX_USE_V1 -I"$LGFX_SOURCE" "${extra[@]}" \
    tests/touch_controls.cpp src/cyd_controls.cpp src/theme.cpp -Wl,--gc-sections \
    -o "$TOUCH_BUILD_DIR/controls"
"$TOUCH_BUILD_DIR/viewport"
"$TOUCH_BUILD_DIR/controls"
