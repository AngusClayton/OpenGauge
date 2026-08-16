# Contributing to OpenGauge

Thank you for improving OpenGauge.

## Before you start

- Discuss large changes in an issue first.
- Keep one pull request focused on one problem.
- Do not commit build output from `.pio`.
- Keep the existing source style where practical.
- Use clear names and short comments. Explain why a change is needed.

## Test your change

Run the checks that match your change:

```sh
pio run
```

For a web UI or renderer change, build the WASM module before you build the
filesystem image:

```sh
bash tools/wasm/build_wasm.sh
node tools/wasm/smoke_test.mjs
pio run --target buildfs
```

On Windows, use `tools/wasm/build_wasm.ps1` instead of the shell script.
Emscripten must be active. Do not commit `.pio` output or generated WASM
assets. CI generates release copies from the C++ source.

For display changes, generate a matching preview with
`tools/gauge_preview.py`. Include an updated PNG when the visual result
changes.

## Check the change impact

OpenGauge uses one C++ renderer on the device, in the native preview tool, and
in the browser WASM module. Do not copy drawing logic into Python or
JavaScript. Change `src/GaugeRenderer.cpp`, then rebuild each preview target.

Use this table to find the files that a change can affect:

| Change | Required checks and updates |
| --- | --- |
| Change an existing screen layout | Update `src/GaugeRenderer.cpp`. Update checked-in PNG previews. Rebuild and test WASM. |
| Add a configurable field | Update the C++ configuration structures and `src/ConfigCodec.cpp`. Update the web editor, Python mock validator, configuration guide, and tests. |
| Add a gauge type | Complete the new-gauge checklist below. |
| Add a default gauge | Update both embedded and tool default JSON documents. Update the user documentation and previews. |
| Add a data source or OBD dependency | Update source lookup or PID scheduling as applicable. Update default JSON and configuration documentation. |
| Change the web UI only | Test the Python mock server and the on-device LittleFS build. Check narrow mobile widths. |

### Add a gauge type

Use one stable JSON type name. For example, use `lapTimer`, not different
names in C++, Python, and JavaScript.

1. Add the enum value and configuration fields in `include/GaugeConfig.h`.
2. Add the type name, validation rules, and field parsing in
   `src/ConfigCodec.cpp`.
3. Declare and implement the renderer in `include/GaugeRenderer.h` and
   `src/GaugeRenderer.cpp`.
4. Add the device render dispatch in `renderDisplay()`.
5. Update `src/ConfigManager.cpp` if the type needs special OBD PIDs, shift
   targets, timer state, or other runtime setup.
6. Add the type and its fields to `tools/configurator/web/app.js`. Add useful
   editable preview samples.
7. Add the same validation rules to
   `tools/configurator/config_model.py`. This keeps the Python mock server
   consistent with the device.
8. Add native preview dispatch and simulated inputs to
   `tools/host/preview_main.cpp`.
9. Add WASM dispatch and simulated state to `tools/wasm/renderer_wasm.cpp`.
   Do not add drawing logic there.
10. Add the type to `docs/CONFIGURATION.md` and `tools/README.md`.
11. If the type is part of the default set, update
    `src/ConfigCodec.cpp` and `tools/configurator/default_config.json`.
12. Add parser tests and a representative PNG preview.

Run all applicable checks:

```sh
pio run
python tools/configurator/server.py
python tools/gauge_preview.py --help
```

Then build WASM, run `tools/wasm/smoke_test.mjs`, and build LittleFS as shown
above. Test Save & apply on a device when the JSON contract or runtime setup
changes.

## Hardware contributions

PCB files, wiring drawings, and test results are welcome after the hardware
release process is defined. Include board revision, tool version, and clear
photographs or renders where possible. Do not publish vehicle-specific wiring
instructions without safety notes.

## Pull requests

Describe the problem, the change, and how you tested it. State any hardware,
vehicle, or sensor assumptions. Update the relevant documentation when the
user-visible behaviour changes.

## Create a release

Only maintainers create releases. Complete these steps from a clean `main`
branch:

1. Confirm that the **Build OpenGauge** workflow passes.
2. Create an annotated version tag, such as `v1.0.0`.
3. Push the tag to GitHub.
4. Confirm that the **Release OpenGauge** workflow publishes the ZIP, binary
   files, and checksums.
5. Test the release with the web installer before you announce it.

The web installer always uses the most recent GitHub Release. Do not move a
release tag after publication. Publish a new version tag for each correction.
