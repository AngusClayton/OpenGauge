# Native gauge screen preview

This tool compiles and runs the production `GaugeRenderer.cpp`, `GUI_Paint.cpp`,
and Nokia font source on the desktop, then converts the exact 240×240 RGB565
framebuffer into PNG. Hardware-only calls are replaced by small host stubs.

It requires Python 3 and `g++`, but no Python packages, Arduino framework,
PlatformIO build, ESP32 compiler, or downloaded dependencies. The host executable
is rebuilt automatically when one of its C++ inputs changes.

```powershell
python tools/gauge_preview.py --type standard --main-source boostPress --min -10 --max 25 --unit "Boost (PSI)" --boost-units --value boostPress=12.4 --value waterTemp=92 --secondary "waterTemp,Water: ,C,180,dynamic" -o boost.png --scale 3
python tools/gauge_preview.py --type shiftlight --value rpm=5900 --value speed=103 -o shift.png --scale 3
python tools/gauge_preview.py --type gmeter --value lateralG=-0.72 --value longitudinalG=0.31 -o gmeter.png --scale 3
```

`--scale` enlarges physical pixels with nearest-neighbour scaling. A secondary
metric has the form `source,prefix,suffix,y[,dynamic]` and can be repeated.

## What updates automatically

Changes inside the existing production renderer functions, drawing primitives,
RGB565 colors, and Nokia fonts appear automatically in the next preview. The
Python file contains no gauge drawing implementation.

When adding a completely new gauge type, expose or forward-declare its production
render function and add one dispatch branch in `tools/host/preview_main.cpp`. Add
CLI-to-stub mappings only for any new sensor inputs it needs. The screen itself is
still rendered solely by the production C++ function.
