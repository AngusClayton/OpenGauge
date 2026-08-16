# OpenGauge

OpenGauge is an ESP32-S3 vehicle display. It reads OBD-II data over CAN, reads
supported analogue sensors, and shows live information on a 240 x 240 round
LCD. Touch gestures change the active gauge screen.

The project is for builders who want to adapt the display, sensor inputs, and
gauge layouts to their own vehicle.

> [!WARNING]
> This project is experimental vehicle electronics. Use suitable fuses, power
> protection, and CAN wiring. Do not use the display as the only source of
> vehicle safety information. Work on a stationary vehicle when you test it.

## Current features

- OBD-II polling over the ESP32 TWAI CAN controller.
- Standard gauges for boost, airflow, AFR, and ignition timing.
- A shift-light screen with gear, RPM, and speed.
- An automatic 0-100 km/h timer that uses OBD speed.
- A G-meter with a five-second dot trail and signed peak readings.
- Analogue sensor inputs with linear calibration.
- Swipe navigation on the integrated touch display.
- Native and browser-WASM previews that run the production C++ renderer.
- A temporary WPA2 configuration hotspot with live save and apply.

## Hardware status

The firmware targets a Waveshare ESP32-S3 1.28-inch touch LCD. The repository
includes KiCad daughter-board files, fabrication outputs, enclosure models,
connector pinouts, and a prototype bill of materials. See
[Hardware](docs/HARDWARE.md), [BOM](docs/BOM.md), and [CAD files](cad/README.md).

## Quick start with VS Code

1. Install Visual Studio Code.
2. Install the **PlatformIO IDE** extension.
3. Clone this repository and open its root folder in Visual Studio Code.
4. Open `platformio.ini`. Set `upload_port` and `monitor_port` if automatic
   port detection does not work.
5. Connect the board.
6. Select the PlatformIO icon in the Activity Bar.
7. Open **Project Tasks > waveshare_esp32s3_touch_lcd_128 > General**.
8. Select **Build**.
9. Select **Upload** to write the firmware.
10. Select **Monitor** to open the serial monitor at 115200 baud.

The PlatformIO toolbar also has Build, Upload, and Monitor buttons. The Upload
task writes the firmware only. It does not write the configuration web files.

## Quick start with the command line

Run these commands from the repository root:

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

If `pio` is not on `PATH`, open a PlatformIO Core CLI terminal from Visual
Studio Code and run the commands there.

## Install the configuration web UI

The web UI is a separate LittleFS image. Build the WASM renderer before you
build the filesystem image. If you do not build the WASM files, the editor
works but exact browser previews show **WASM asset not installed**.

For most users, install the latest tagged release with the
[OpenGauge web installer](https://angusclayton.github.io/OpenGauge/). It writes
the firmware and the web interface to a supported ESP32-S3 board. Chrome or
Edge with Web Serial support is required.

Permanent release packages are on the GitHub **Releases** page. Each release
contains a merged factory image for browser installation, separate firmware
and LittleFS images for manual installation, boot files, checksums, the WASM
renderer, and a complete ZIP package. The **Build OpenGauge** workflow also
produces a 14-day development artifact for each commit.

For a local maintainer build:

1. Install and activate Emscripten so that `em++` is available.
2. Run the applicable WASM build script:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools/wasm/build_wasm.ps1
   ```

   ```sh
   bash tools/wasm/build_wasm.sh
   ```

3. In the PlatformIO Task Explorer, open the project environment.
4. Select **Platform > Build Filesystem Image** if this task is available.
5. Select **Platform > Upload Filesystem Image**. Some PlatformIO versions
   show **Upload File System image**.

The command-line equivalents are:

```sh
pio run --target buildfs
pio run --target uploadfs
```

> [!CAUTION]
> The web installer and Upload Filesystem Image task replace the complete
> LittleFS partition. They can delete the saved `/config.json` file. Download
> the current JSON from the web UI before installation.

Do not connect the device to a vehicle until you have checked its power,
ground, CAN wiring, and sensor calibration.

## Configure a gauge

Swipe up on the gauge and start its temporary configuration hotspot. The gauge
shows the device-specific Wi-Fi name and password. The captive portal can edit,
preview, upload, download, save, and apply the versioned JSON without rebooting.
The firmware pauses analogue input sampling while the hotspot is active.
See [Configuration](docs/CONFIGURATION.md) and the
[configuration portal](tools/configurator/README.md).

## Preview screens without an ESP32 build

The preview tool runs the production gauge renderer with desktop stubs. It
does not require the Arduino or ESP32 toolchain.

```powershell
python tools/gauge_preview.py --type standard --main-source boostPress --min -10 --max 25 --unit "Boost (PSI)" --boost-units --value boostPress=12.4 --value waterTemp=92 --value intakeTemp=31 --secondary "waterTemp,Water: ,C,range,80,105,blue,cyan,red" --secondary "intakeTemp,AIT: ,C" -o preview.png --scale 3
```

See [Preview tool](tools/README.md) for all options. The checked-in examples
are in [previews](previews).

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/` | Firmware source files and display drivers. |
| `include/` | Shared firmware headers. |
| `tools/` | Native renderer and PNG preview tool. |
| `previews/` | Example screen renders. |
| `docs/` | Hardware and configuration documentation. |
| `cad/` | KiCad PCB files, fabrication outputs, and enclosure models. |
| `.github/workflows/` | Automated firmware, WASM, parser, and filesystem checks. |

## Contribute

Small, focused changes are easiest to review. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before you open a pull request.

## License

OpenGauge uses strong reciprocal licences:

- Original software is licensed under GPL-3.0-or-later. Distributed software
  modifications must remain available under the same licence.
- Original hardware design material is licensed under CERN-OHL-S-2.0.
  Distributed modified designs and products must meet its source obligations.

See [LICENSE](LICENSE) for the precise scope, the complete texts in
[LICENSES](LICENSES), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for
components that retain separate notices.
