#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
arduino_json="$root/.pio/libdeps/waveshare_esp32s3_touch_lcd_128/ArduinoJson/src"
test -d "$arduino_json" || { echo 'Run PlatformIO dependency installation first.' >&2; exit 1; }
em++ \
  "$root/src/GaugeRenderer.cpp" "$root/src/ConfigCodec.cpp" \
  "$root/src/Display/GUI_Paint.cpp" "$root/src/Display/Font_nokia.cpp" \
  "$root/tools/wasm/renderer_wasm.cpp" \
  -std=c++17 -O3 -flto \
  -I"$root/tools/host/include" -I"$root/include" -I"$root/src" -I"$arduino_json" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 \
  -sEXPORTED_FUNCTIONS="['_og_renderer_abi_version','_og_render','_og_framebuffer_ptr','_og_framebuffer_size','_og_last_error']" \
  -sEXPORTED_RUNTIME_METHODS="['ccall','UTF8ToString']" \
  -o "$root/tools/configurator/web/opengauge-renderer.js"
