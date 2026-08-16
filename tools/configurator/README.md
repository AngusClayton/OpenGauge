# Configuration portal

This web application is shared by the local development server and the
OpenGauge configuration hotspot. The local server writes only its ignored test
configuration. On the gauge, **Save & apply** writes the versioned document to
LittleFS and activates it without a reboot.

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

The on-device portal loads a WebAssembly build of the production C++ renderer.
JavaScript converts its RGB565 framebuffer to browser pixels; it does not
reimplement the layout. When the Python server has no WASM build, its preview
endpoint invokes `tools/gauge_preview.py` and returns a PNG.

## On-device use

1. Swipe up on the round display.
2. Tap **Start hotspot**.
3. Join the device-specific `OpenGauge-XXXXXX` WPA2 network with the password
   shown on the display.
4. Use the captive portal or open <http://192.168.4.1>.

The password is generated once and stored in NVS. The hotspot stops after ten
minutes without portal or touch activity, or when **Exit hotspot** is selected.

The web directory is also PlatformIO's filesystem data directory. Upload it
with `platformio run --target uploadfs`.

Normal users do not need Emscripten. CI builds the WASM module. Maintainers can
use `tools/wasm/build_wasm.ps1` or `tools/wasm/build_wasm.sh`.

## Configuration document

`default_config.json` is the initial version-1 document. It has two top-level
arrays:

- `dataSources` for OBD and analogue values.
- `gauges` for the swipe-ordered screens.

This is the version-1 device contract. Unsupported versions are rejected.
