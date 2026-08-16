# Bill of materials

This BOM applies to the prototype files in `cad/PCB`. The generated source BOM
is [cad/PCB/production/bom.csv](../cad/PCB/production/bom.csv).

## Complete unit

| Quantity | Item | Notes or source |
| ---: | --- | --- |
| 1 | Waveshare ESP32-S3-Touch-LCD-1.28 development board | [Core Electronics](https://core-electronics.com.au/esp32-s3-development-board-128inch-round-touch-lcd.html) |
| 1 | OpenGauge daughter PCB | Make from `cad/PCB/production/daughter-board.zip`, or export new fabrication files from KiCad. |
| 1 | 10 cm, 12-position, double-ended 1.0 mm cable | [Example source](https://www.aliexpress.com/item/1005007277314092.html). Verify SHL mating compatibility, pin-1 orientation, and straight-through wiring. |
| 5 | Complete 3-position, 3.81 mm plug and header pairs, 15EDG type | [Example source](https://www.aliexpress.com/item/1005002951702737.html). J2 to J6 use the PCB headers. |
| 1 | Printed main body | `cad/Case/mainBody.3mf` |
| 1 | Printed front ring | `cad/Case/frontRing.3mf` |
| 6 | M3 x 10 mm button-head screws |  |

## Vehicle installation parts

| Quantity | Item | Notes |
| ---: | --- | --- |
| 1 | Male OBD-II plug or breakout harness | Must provide verified access to CAN-H pin 6 and CAN-L pin 14 for a standard ISO 15765-4 connection. |
| 1 | Add-a-circuit or piggyback fuse tap | Select the fuse type that matches the vehicle fuse box. |
| 1 | 1 A fuse | Fit in the OpenGauge branch of the fuse tap. |
| As required | Automotive wire, terminals, insulation, and strain relief | Select these parts for the circuit, temperature, vibration, and installation method. |

Retain the vehicle circuit's original fuse and rating in the fuse tap. Do not
use OBD-II pin 16 as the normal power source because it is often connected to
constant battery power. See [Connect to a vehicle](HARDWARE.md#connect-to-a-vehicle).

## Daughter-board components

| References | Quantity | Value or part | PCB footprint | Selection notes |
| --- | ---: | --- | --- | --- |
| C1, C2 | 2 | 10 uF | 0805 | Minimum 25 V rating. |
| C6, C7, C10 | 3 | 1 uF | 0805 | Minimum 25 V rating. |
| C3 | 1 | 4.7 nF | 0805 | Split CAN termination capacitor. |
| C4, C5, C8, C9 | 4 | 0.1 uF | 0805 | Decoupling capacitors. |
| D1 | 1 | Diode, exact part not specified | SMA | Use a Schottky diode rated for at least 40 V and 3 A. |
| J1 | 1 | 12-position JST SHL socket | `SM12B-SHLS-TF`, 1.0 mm horizontal | Confirm mating cable series and orientation. |
| J2, J3, J4, J5, J6 | 5 | 3-position connector | 3.81 mm vertical header | Part of the five terminal pairs. |
| R1, R2 | 2 | 60 ohm | 0805 | Split CAN termination. Review before vehicle installation. |
| R10 | 1 | 2.5 kohm | 0805 | BL input divider. |
| R3, R4, R5, R6, R7, R8, R9 | 7 | 10 kohm | 0805 | Analogue and BL dividers. |
| U1 | 1 | L7805 | TO-252-3, tab is pin 2 | Select a pin-compatible 5 V regulator and check heat loss. |
| U2 | 1 | TJA1051T/3 | SOIC-8, 3.9 x 4.9 mm, 1.27 mm pitch | Use the `/3` VIO version shown in the schematic. |

The PCB footprint is for the JST SHL series `SM12B-SHLS-TF` header. 


## Items not specified

The current design does not specify these items:

- Vehicle fuse and fuse holder.
- Vehicle-side wire, terminals, and connector shells.
- Cable strain relief.
- PCB material, copper weight, finish, and assembly class.
- Enclosure print material and print settings.
- Exact diode and passive manufacturer part numbers.
- Conformal coating or environmental sealing.

Record tested selections in the KiCad BOM fields. Do not rely only on this
Markdown file for a production purchase.
