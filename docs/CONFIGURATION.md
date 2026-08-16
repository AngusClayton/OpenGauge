# Configuration

The current configuration is embedded JSON in
[`src/ConfigManager.cpp`](../src/ConfigManager.cpp). The firmware loads it at
startup. Edit the JSON, build the firmware, and upload it to apply a change.

## Data sources

`varsJson` defines each value that a gauge can use.

| Field | Use |
| --- | --- |
| `id` | Unique name used by a gauge profile. |
| `type` | `obd` or `analog`. |
| `pid` | OBD-II PID number for an `obd` source. |
| `formula` | OBD conversion formula index. |
| `pin` | ESP32 GPIO number for an `analog` source. |
| `multiplier` | Analogue calibration slope. |
| `offset` | Analogue calibration offset. |

Example OBD temperature source:

```json
{ "id": "waterTemp", "type": "obd", "pid": 5, "formula": 2 }
```

Example analogue pressure source:

```json
{ "id": "boostPress", "type": "analog", "pin": 18, "multiplier": 30.7692, "offset": -28.2692 }
```

The analogue conversion is:

```text
value = (sensor volts x multiplier) + offset
```

Calibrate the multiplier and offset against the sensor data sheet and a known
reference. The current hardware uses a voltage divider. Check the hardware
before you connect a sensor.

## Gauge profiles

`gaugesJson` defines the screens. A gauge profile uses one of these types:

| Type | Use |
| --- | --- |
| `standard` | Main value, unit, and up to three secondary values. |
| `shiftlight` | Gear, RPM, speed, and the shift arc. |
| `gmeter` | Live G-force dot, five-second trail, and peak readings. |
| `accelTimer` | Automatic 0-100 km/h timer that uses the `speed` source. |

Shift-light profiles use `shiftTargets`, an array containing the target RPM for
gears 1 through 6. Use `0` when a gear has no shift target.

Standard-gauge fields:

| Field | Use |
| --- | --- |
| `name` | Profile name used in logs. |
| `mainSourceId` | Source for the main value and arc. |
| `minVal`, `maxVal` | Arc range. |
| `unitLabel` | Text below the main value. |
| `boostUnits` | Shows inHg for negative boost values and PSI for positive values. |
| `secondaries` | List of up to three secondary values. |

A secondary object has these fields:

| Field | Use |
| --- | --- |
| `sourceId` | Source for the value. |
| `prefix` | Label shown below the secondary value. A trailing colon is removed. |
| `suffix` | Unit shown with the value. |
| `rangeColors` | Enables value-based colouring for this reading. |
| `lowerThreshold` | Values below this boundary use `colorBelow`. |
| `upperThreshold` | Values above this boundary use `colorAbove`. |
| `colorBelow` | Colour used below the lower threshold. |
| `colorBetween` | Colour used between the two thresholds. |
| `colorAbove` | Colour used above the upper threshold. |

Supported colour names are `white`, `gray`, `blue`, `cyan`, `green`, `yellow`,
`orange`, and `red`. The range system is not temperature-specific. It can show
safe and warning ranges for pressure, temperature, fluid level, or any other
numeric source.

Example boost profile:

```json
{
  "type": "standard",
  "name": "Gauge 1: Boost",
  "mainSourceId": "boostPress",
  "minVal": -10.0,
  "maxVal": 25.0,
  "unitLabel": "Boost (PSI)",
  "boostUnits": true,
  "secondaries": [
    { "sourceId": "waterTemp", "prefix": "Water: ", "suffix": "C", "rangeColors": true, "lowerThreshold": 80, "upperThreshold": 105, "colorBelow": "blue", "colorBetween": "cyan", "colorAbove": "red" },
    { "sourceId": "intakeTemp", "prefix": "AIT: ", "suffix": "C", "rangeColors": false }
  ]
}
```

## Limits

The current fixed-size configuration supports up to 10 data sources, 10 gauge
profiles, and 3 secondary values per profile. A standard gauge should use no
more than three secondary values so each value can stay readable.

## 0-100 timer

The default `accelTimer` profile uses the OBD `speed` source. It arms at the
configured `minVal`, starts when speed rises above it, and stops at `maxVal`.
The result remains on screen until speed returns to `minVal`. While this profile
is active, the firmware requests speed at 50 Hz and updates the timer from the
OBD task, not the display task.

Use a profile like this:

```json
{
  "type": "accelTimer",
  "name": "Gauge 7: 0-100 Timer",
  "mainSourceId": "speed",
  "minVal": 0.0,
  "maxVal": 100.0,
  "unitLabel": "km/h"
}
```

## Preview a change

Use the native renderer to check a screen before you upload firmware:

```powershell
python tools/gauge_preview.py --type standard --main-source boostPress --min -10 --max 25 --unit "Boost (PSI)" --boost-units --value boostPress=12.4 -o boost.png --scale 3
```

See [Preview tool](../tools/README.md) for more examples.
