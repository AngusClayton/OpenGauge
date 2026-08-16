# Hardware

## Status

The `cad` directory contains the current OpenGauge prototype hardware:

- KiCad schematic, PCB, and project files.
- PCB fabrication outputs and assembly data.
- STEP and 3MF enclosure files.
- A generated PCB BOM and component position file.

This is a prototype reference design. It is not an automotive-qualified
product. Review the schematic, PCB, and selected components before you make or
install a unit.

See the [bill of materials](BOM.md) for component quantities and purchasing
notes. See the [CAD guide](../cad/README.md) for the source-file layout.

## System parts

OpenGauge uses these main assemblies:

| Assembly | Function |
| --- | --- |
| Waveshare ESP32-S3-Touch-LCD-1.28 | Processor, round display, touch controller, and IMU. |
| OpenGauge daughter board | Power input, CAN transceiver, analogue input conditioning, and connectors. |
| 12-pin JST SH 1.0 mm cable | Connects the daughter board to the Waveshare board. |
| Printed enclosure | Holds the display and daughter board. |

The Waveshare board has 16 MB flash, 2 MB PSRAM, a 240 x 240 display, a CST816S
touch controller, and a QMI8658 IMU. Refer to the
[Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28)
before you substitute the board.

## Daughter-board circuits

### Power

J2 is the power and illumination connector. D1 is in series with the 12 V
input. U1 is an L7805 in a TO-252 package and supplies the 5 V rail. The 5 V
rail supplies the Waveshare board, the CAN transceiver, and the external sensor
connectors.


### CAN

U2 is a TJA1051T/3 CAN transceiver in an SOIC-8 package. It uses 5 V for the
transceiver supply and 3.3 V for its VIO supply. The firmware uses GPIO16 for
CAN TX and GPIO15 for CAN RX.

R1 and R2 are 60 ohm resistors. They form split CAN termination. C3 connects
the termination midpoint to ground. The assembled board therefore has a fixed
nominal 120 ohm termination across CAN-H and CAN-L.


### Analogue inputs

J4, J5, and J6 each supply 5 V, one signal input, and ground. Each signal uses
a 10 kΩ/10 kΩ divider and a 1 uF filter capacitor. The divider gives the ESP32
approximately one half of the connector input voltage. The firmware applies a
factor of two before it applies the configured multiplier and offset.

The default firmware uses:

| Connector | PCB signal | ESP32 GPIO | Default use |
| --- | --- | --- | --- |
| J4 | IO_1 | GPIO21 | Not assigned. |
| J5 | IO_3 | GPIO17 | Spare analogue input. |
| J6 | IO_2 | GPIO18 | Analogue boost input. |

Do not apply a voltage until you verify the sensor output, divider, ESP32 input
range, and common ground.

## Connector pinout

Pin numbers follow the KiCad PCB footprints. Check the PCB pin-1 mark before
you connect a cable. Do not use wire colour as the only pin reference.

### J1: Waveshare board

J1 is a 12-position, 1.0 mm-pitch JST SHL horizontal socket.

| Pin | PCB net | Waveshare function |
| ---: | --- | --- |
| 1 | GND | Ground |
| 2 | +5V | VSYS / 5 V input |
| 3 | Not connected | Not used by this PCB; confirm the function for the Waveshare board revision. |
| 4 | Not connected | Not used by this PCB; confirm the function for the Waveshare board revision. |
| 5 | GND | Ground |
| 6 | +3V3 | 3.3 V |
| 7 | CAN-RX | GPIO15 |
| 8 | CAN-TX | GPIO16 |
| 9 | IO_3_MCU | GPIO17 |
| 10 | IO_2_MCU | GPIO18 |
| 11 | IO_1_MCU | GPIO21 |
| 12 | BL_MCU | GPIO33 |

Confirm the cable orientation against both connector pin-1 marks. A reversed
cable can put a supply voltage on a signal pin.

### J2: power and illumination

| Pin | Function |
| ---: | --- |
| 1 | Supply input through D1 to the +12 V rail |
| 2 | Ground |
| 3 | BL illumination input |

The BL input is divided by R9 and R10 before it reaches GPIO33. Confirm its
permitted voltage range before connection.

### J3: CAN

| Pin | Function |
| ---: | --- |
| 1 | CAN-L |
| 2 | CAN-H |
| 3 | Ground |

### J4, J5, and J6: sensor inputs

| Pin | J4 | J5 | J6 |
| ---: | --- | --- | --- |
| 1 | +5 V | +5 V | +5 V |
| 2 | IO_1 input | IO_3 input | IO_2 input |
| 3 | Ground | Ground | Ground |

## Firmware interfaces

| Interface | Current use |
| --- | --- |
| GPIO15 | CAN RX |
| GPIO16 | CAN TX |
| GPIO17 | Spare analogue input |
| GPIO18 | Analogue boost input |
| GPIO33 | Illumination input |
| QMI8658 | G-meter data |

The CAN definitions are in `src/obd/obd.h`. The analogue definitions are in
`src/Sensors.cpp`. Check the source and PCB revision before you wire a device.

## Enclosure

The `cad/Case` directory contains:

- `mainBody.3mf`
- `frontRing.3mf`
- `OpenGauge.step`

The assembly uses six M3 x 10 mm button-head screws. The current files do not
specify screw material, inserts, print material, layer height, wall count, or
environmental rating. Select these items for the installation environment and
record the result when you publish a tested build.

## Connect to a vehicle

Use a male OBD-II plug or breakout harness for the CAN connection. Identify
the pin numbers from the moulded marks on the connector. Do not identify pins
only from wire colour or from the apparent left-to-right order in a photograph.

For the standard ISO 15765-4 high-speed CAN position:

| Vehicle OBD-II pin | Signal | OpenGauge connection |
| ---: | --- | --- |
| 6 | CAN-H | J3 pin 2 |
| 14 | CAN-L | J3 pin 1 |

OBD-II pin 4 is chassis ground, pin 5 is signal ground, and pin 16 is battery
power. The PCB grounds on J2 and J3 are common. Use the vehicle wiring diagram
to select one suitable ground scheme. Avoid multiple ground paths. Connect J3
pin 3 only when the vehicle wiring plan requires a CAN reference ground.

Not all vehicles expose the required bus on OBD-II pins 6 and 14. Some vehicles
use manufacturer-specific pins, a gateway, or a different diagnostic protocol.
Check the vehicle service information before connection.

### Check CAN termination first

The current daughter board fits R1 and R2 as a permanent 120 ohm split
termination. CAN termination belongs at the two ends of the main bus. An
OBD-II diagnostic device is usually a short stub and usually must not add a
third termination.

Before you connect the daughter board to a vehicle, determine whether R1 and
R2 must be left unpopulated. Do not connect the populated termination until a
person who understands that vehicle network confirms that it is required.

For context - system works fine on a mk5 golf with termination resistors on pcb.

### Connect accessory-switched power

Do not use OBD-II pin 16 as the normal OpenGauge power source. Pin 16 is often
connected to constant battery power and can keep the unit on when the vehicle
is off.

Use an add-a-circuit, also called a piggyback fuse tap, on a suitable
accessory-switched fuse position:

1. Read the vehicle service information and the fuse-tap instructions.
2. Select a non-safety-critical accessory circuit. Do not use an airbag, ABS,
   engine-control, steering, braking, or other safety circuit.
3. Use a multimeter to confirm that the selected position is off when the
   vehicle is off and live in the required accessory or ignition state.
4. Install the original circuit fuse with its original rating in the correct
   fuse-tap position.
5. Install a 1 A fuse for the new OpenGauge branch.
6. Install the fuse tap in the correct supply and load orientation. An
   incorrectly oriented tap can bypass the new branch fuse.
7. Connect the fused accessory wire to J2 pin 1.
8. Connect J2 pin 2 to a verified vehicle chassis ground point.
9. Insulate and secure all joints. Add strain relief and keep the harness away
   from pedals, steering parts, sharp edges, heat, and airbag deployment paths.
10. Confirm that OpenGauge switches off in all intended key-off states.

Leave J2 pin 3 disconnected unless you have verified the vehicle illumination
signal voltage and want to use the BL input.

> [!WARNING]
> A 1 A fuse protects the new branch only when the fuse tap, wire gauge,
> connector, and installation are correct. If you are not qualified to modify
> vehicle wiring, use a qualified automotive electrician.

## Vehicle installation

Use an appropriate fuse and an automotive-rated supply and protection design.
Keep CAN wiring as a twisted pair. Connect signal grounds as the sensor and
vehicle manufacturer specify. Provide strain relief for all cables.

Do not rely on OpenGauge for a safety-critical warning. Do not change vehicle
wiring while the vehicle is moving.
