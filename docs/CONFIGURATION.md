# Configuration

The firmware stores the active versioned JSON document in LittleFS. Portal
changes are validated and applied live. At startup, the firmware tries the
current file, its backup, and finally the embedded defaults, so an interrupted
or invalid update remains recoverable.

Use **Download JSON** before you upload a new filesystem image. The PlatformIO
Upload Filesystem Image task replaces LittleFS and can remove the saved
configuration. See the [configuration portal guide](../tools/configurator/README.md)
for the VS Code and command-line procedures.

## Data sources

`dataSources` defines each value that a gauge can use.

| Field | Use |
| --- | --- |
| `id` | Unique name used by a gauge profile. |
| `type` | `obd` or `analog`. |
| `pid` | OBD-II PID number for an `obd` source. |
| `formula` | OBD conversion formula index. |
| `pin` | ESP32 GPIO number for an `analog` source. |
| `multiplier` | Multiply the decoded OBD value or analogue voltage by this number. |
| `offset` | Add this number after multiplication. |

Example OBD temperature source:

```json
{ "id": "waterTemp", "type": "obd", "pid": 5, "formula": 2 }
```

Example analogue pressure source:

```json
{ "id": "boostPress", "type": "analog", "pin": 18, "multiplier": 30.7692, "offset": -28.2692 }
```

OBD sources have independent conversions, even when they use the same PID.
The bus polls each PID once per schedule cycle. Each source applies its own
formula, multiplier, and offset to the cached response bytes A and B.

| Formula | Decoded value before multiplier and offset |
| --- | --- |
| 0 or 6 | A |
| 1 or 7 | A * 256 + B |
| 2 | A - 40 |
| 3 | (A * 256 + B) / 4 |
| 4 | (A * 256 + B) / 100 |
| 5 | A / 2.55 |

Use separate source IDs for different calculations:

```json
{"id":"maf","type":"obd","pid":16,"formula":4,"multiplier":1,"offset":0}
{"id":"hp","type":"obd","pid":16,"formula":4,"multiplier":1.25,"offset":0}
{"id":"lambda","type":"obd","pid":52,"formula":7,"multiplier":0.000030517578125,"offset":0}
{"id":"afr","type":"obd","pid":52,"formula":7,"multiplier":0.0004486083984375,"offset":0}
```

These are individual source objects. Add them to the `dataSources` array.
Set the HP screen main source to `hp` and its secondary source to `maf`.
The default HP estimate is MAF in g/s multiplied by 1.25; it is an estimate,
not a measured engine output. Adjust the factor for your application.
The default AFR conversion uses gasoline stoichiometry of 14.7.

Saved configurations are retained during firmware updates. For an existing
HP screen, add the `hp` source and change its main source from `maf` to `hp`.
Add explicit multiplier and offset fields to existing AFR/lambda sources.
For old version-1 sources without these fields, PID 52/formula 7 keeps the
lambda conversion (or gasoline AFR for ID `afr`), and PID 14/formula 6 keeps
A / 2 - 64. Other OBD sources default to multiplier 1 and offset 0.
Explicit settings allow any source ID, without name-dependent conversion.
Preview sample values are final display values per source ID; previews do
not simulate raw OBD responses.

The analogue conversion is:

```text
value = (sensor volts x multiplier) + offset
```

Calibrate the multiplier and offset against the sensor data sheet and a known
reference. The current hardware uses a voltage divider. Check the hardware
before you connect a sensor.

## Gauge profiles

`gauges` defines the ordered screens. A gauge profile uses one of these types:

| Type | Use |
| --- | --- |
| `standard` | Main value, unit, and up to three secondary values. |
| `shiftlight` | Gear, RPM, speed, and the shift arc. |
| `gmeter` | Live G-force dot, five-second trail, and peak readings. |
| `accelTimer` | Automatic 0-100 km/h timer that uses the `speed` source. |

Shift-light profiles use `shiftTargets`, an array containing the target RPM for
gears 1 through 6. Use `0` when a gear has no shift target.

G-meter profiles use `maxG` for the outer ring scale, from 0.1 to 16 g.
The default is 1.5 g. The inner ring shows half this value. Both the live dot
and the trail use the same scale. Values beyond the outer ring stay on its
edge and keep their direction. Peak numbers show the measured values even
when they exceed the scale. This setting changes the display, not the IMU
measurement range. For example:

```json
{"type":"gmeter","name":"G Meter","maxG":2.0}
```

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
