$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$arduinoJson = Join-Path $root '.pio\libdeps\waveshare_esp32s3_touch_lcd_128\ArduinoJson\src'
if (-not (Get-Command em++ -ErrorAction SilentlyContinue)) { throw 'Emscripten em++ is required. Use the emsdk or the repository CI workflow.' }
if (-not (Test-Path $arduinoJson)) { throw 'Run a PlatformIO dependency install first so ArduinoJson is available.' }
$output = Join-Path $root 'tools\configurator\web\opengauge-renderer.js'
$sources = @(
  (Join-Path $root 'src\GaugeRenderer.cpp'), (Join-Path $root 'src\ConfigCodec.cpp'),
  (Join-Path $root 'src\Display\GUI_Paint.cpp'), (Join-Path $root 'src\Display\Font_nokia.cpp'),
  (Join-Path $root 'tools\wasm\renderer_wasm.cpp')
)
& em++ @sources -std=c++17 -O3 -flto `
  "-I$(Join-Path $root 'tools\host\include')" "-I$(Join-Path $root 'include')" "-I$(Join-Path $root 'src')" "-I$arduinoJson" `
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 `
  "-sEXPORTED_FUNCTIONS=['_og_renderer_abi_version','_og_render','_og_framebuffer_ptr','_og_framebuffer_size','_og_last_error']" `
  "-sEXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']" -o $output
Write-Host "Built $output"
