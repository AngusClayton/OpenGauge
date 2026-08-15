# 32GAUGE

32GAUGE is an ESP32-S3 vehicle display. It reads OBD-II data over CAN, reads
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
- A G-meter with a five-second dot trail and signed 30-second peak readings.
- Analogue sensor inputs with linear calibration.
- Swipe navigation on the integrated touch display.
- Native PNG previews that run the production C++ renderer on a desktop.

## Hardware status

The firmware targets a Waveshare ESP32-S3 1.28-inch touch LCD configuration.
The repository does not yet include PCB files, a bill of materials, wiring
drawings, or installation instructions. See [Hardware](docs/HARDWARE.md) for
the current interface notes and planned hardware documentation.

## Quick start

1. Install [PlatformIO](https://platformio.org/).
2. Clone this repository.
3. Review `platformio.ini`. Set your upload and monitor port if required.
4. Connect the board and the required CAN hardware.
5. Build the firmware:

   ```sh
   pio run
   ```

6. Upload the firmware:

   ```sh
   pio run -t upload
   ```

7. Open the serial monitor:

   ```sh
   pio device monitor --baud 115200
   ```

Do not connect the device to a vehicle until you have checked its power,
ground, CAN wiring, and sensor calibration.

## Configure a gauge

The current data-source and gauge-profile JSON is embedded in
[`src/ConfigManager.cpp`](src/ConfigManager.cpp). Update it, then rebuild and
upload the firmware. See [Configuration](docs/CONFIGURATION.md) for the field
reference and examples.

## Preview screens without an ESP32 build

The preview tool runs the production gauge renderer with desktop stubs. It
does not require the Arduino or ESP32 toolchain.

```powershell
python tools/gauge_preview.py --type standard --main-source boostPress --min -10 --max 25 --unit "Boost (PSI)" --boost-units --value boostPress=12.4 --value waterTemp=92 --value intakeTemp=31 --secondary "waterTemp,Water: ,C,180,dynamic" --secondary "intakeTemp,AIT: ,C,208" -o preview.png --scale 3
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
| `test/` | Firmware tests. |

## Contribute

Small, focused changes are easiest to review. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before you open a pull request.

## License

No licence has been selected for this repository yet. Do not reuse or
redistribute the project until a licence is added.
