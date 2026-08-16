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

- Use a mobile-friendly accordion layout.
- Edit sources and all current gauge types.
- Upload, download, validate, and restore one versioned JSON document.
- Drag screens to change their swipe order.
- Delete a source or screen after a confirmation prompt.
- Edit shift RPM targets for gears 1 through 6.
- Request a PNG preview for unsaved edits.
- Save the document to LittleFS and apply it without a reboot.

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
The firmware pauses GPIO17 and GPIO18 analogue sampling while the hotspot is
active. This prevents ADC2 from interfering with the ESP32-S3 Wi-Fi radio.

## Build and upload the web UI

The `tools/configurator/web` directory is the PlatformIO filesystem data
directory. The filesystem image is separate from the firmware image.

Use the PlatformIO extension in Visual Studio Code:

1. Select the PlatformIO icon in the Activity Bar.
2. Open **Project Tasks > waveshare_esp32s3_touch_lcd_128 > Platform**.
3. Select **Build Filesystem Image** if this task is available.
4. Select **Upload Filesystem Image**. Some PlatformIO versions show **Upload
   File System image**.

Or use the command line:

```sh
pio run --target buildfs
pio run --target uploadfs
```

> [!CAUTION]
> A filesystem upload replaces the complete LittleFS partition. It can remove
> the saved `/config.json` file. Download the JSON before you upload LittleFS.

The firmware uses a separate upload task. In Visual Studio Code, select
**General > Upload**. The command-line equivalent is:

```sh
pio run --target upload
```

Upload both images after a clean installation. Upload only the image that you
changed during normal development.

## Build the exact WASM preview

The web UI needs these generated files for exact browser previews:

- `opengauge-renderer.js`
- `opengauge-renderer.wasm`

Generate them before you build LittleFS. Activate Emscripten and run one
script from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools/wasm/build_wasm.ps1
```

```sh
bash tools/wasm/build_wasm.sh
```

GitHub Actions also builds the WASM module. Maintainers can download
`firmware.bin`, `littlefs.bin`, and the WASM files from the
`opengauge-firmware` workflow artifact. These are development artifacts, not a
complete installer. If the WASM files are absent, the editor still works. The
on-device preview reports **WASM asset not installed**. The local Python server
uses its native PNG fallback.

## Troubleshooting

| Symptom | Cause and action |
| --- | --- |
| The hotspot screen is active, but no network is visible. | Confirm that the current firmware is installed. Check the `[PORTAL]` serial messages. |
| The browser reports that `/index.html` does not exist. | Build and upload the LittleFS image. |
| The editor reports **WASM asset not installed**. | Build the WASM files, then build and upload LittleFS again. |
| A firmware fix does not take effect after `uploadfs`. | Upload the firmware. The filesystem task does not write firmware. |
| Save and apply causes a reset. | Record the full serial panic and the ELF SHA. Confirm that the latest firmware is installed. |

## Configuration document

`default_config.json` is the initial version-1 document. It has two top-level
arrays:

- `dataSources` for OBD and analogue values.
- `gauges` for the swipe-ordered screens.

This is the version-1 device contract. Unsupported versions are rejected.
