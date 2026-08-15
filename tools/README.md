# Native screen preview

`gauge_preview.py` runs the production gauge renderer on a desktop and writes
a PNG. It compiles these production source files:

- `src/GaugeRenderer.cpp`
- `src/Display/GUI_Paint.cpp`
- `src/Display/Font_nokia.cpp`

The tool replaces hardware-only calls with small host stubs. The PNG uses the
same 240 x 240 RGB565 framebuffer as the device. Grey pixels mark the part of
the square framebuffer that is outside the physical round LCD.

## Requirements

- Python 3
- `g++`

The tool does not require PlatformIO, the Arduino framework, or the ESP32
toolchain. It rebuilds the host executable when a renderer input changes.

## Examples

Run these commands from the repository root:

```powershell
# Standard gauge
python tools/gauge_preview.py --type standard --main-source boostPress --min -10 --max 25 --unit "Boost (PSI)" --boost-units --value boostPress=12.4 --value waterTemp=92 --value intakeTemp=31 --secondary "waterTemp,Water: ,C,180,dynamic" --secondary "intakeTemp,AIT: ,C,208" -o boost.png --scale 3

# Shift-light gauge
python tools/gauge_preview.py --type shiftlight --value rpm=5900 --value speed=120 -o shift.png --scale 3

# G-meter with current motion and older peaks
python tools/gauge_preview.py --type gmeter --value lateralG=-0.42 --value longitudinalG=0.18 --value peakLat=-0.95 --value peakLong=0.68 -o gmeter.png --scale 3

# Completed 0-100 km/h run in 7.42 seconds
python tools/gauge_preview.py --type accelTimer --value speed=100 --value timerMs=7420 -o 0-to-100.png --scale 3
```

Use `--scale 3` to enlarge each LCD pixel by three. It does not change the
underlying 240 x 240 render.

## Options

| Option | Use |
| --- | --- |
| `--type` | Select `standard`, `shiftlight`, `gmeter`, or `accelTimer`. |
| `--value NAME=NUMBER` | Set a simulated sensor value. Repeat as needed. |
| `--main-source`, `--min`, `--max`, `--unit` | Set standard-gauge inputs. |
| `--secondary` | Add a standard secondary value: `source,prefix,suffix,y[,dynamic]`. |
| `--boost-units` | Use inHg for negative values and PSI for positive values. |
| `--offline` | Show the offline IMU state. |
| `--output`, `-o` | Set the PNG path. |
| `--scale` | Set integer nearest-neighbour PNG scale. |

For a G-meter, `lateralG` and `longitudinalG` set the current dot. `peakLat`
and `peakLong` simulate signed values from the 30-second peak window.
For an acceleration-timer preview, `speed` sets the final speed and `timerMs`
sets the elapsed test time after the simulated launch.

## Add a new gauge type

Add the production renderer in `src/GaugeRenderer.cpp`. Then add one host
dispatch branch in `tools/host/preview_main.cpp` and any required simulated
inputs. Do not duplicate drawing code in Python.
