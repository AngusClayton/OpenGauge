# Configuration portal prototype

This local web application is the development prototype for the future
OpenGauge configuration hotspot. It does not change firmware, start Wi-Fi, or
write to the ESP32.

## Run

From the repository root, run:

```powershell
python tools/configurator/server.py
```

Open <http://127.0.0.1:8000> in a browser.

The prototype uses only Python standard-library modules. Saved test changes go
to `tools/configurator/local_config.json`, which Git ignores.

## Functions

- Edit sources and all current gauge types.
- Upload, download, validate, and restore one versioned JSON document.
- Reorder screens to change swipe order.
- Edit shift RPM targets for gears 1 through 6.
- Request a PNG preview for unsaved edits.

The preview endpoint invokes `tools/gauge_preview.py`, which compiles and runs
the production C++ renderer. The browser displays the returned PNG. It does
not draw a simulated gauge with JavaScript or canvas.

## Prototype configuration

`default_config.json` is the initial version-1 document. It has two top-level
arrays:

- `dataSources` for OBD and analogue values.
- `gauges` for the swipe-ordered screens.

This document shape is the candidate contract for the later ESP32 storage and
hotspot implementation. Do not treat it as a stable public firmware format
until the prototype is approved.
