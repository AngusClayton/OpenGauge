# Hardware

## Status

Hardware documentation is in progress. PCB files, production gerbers, a bill
of materials, enclosure details, and complete wiring diagrams will be added in
a later release.

## Current firmware interfaces

The firmware currently expects these interfaces:

| Interface | Current use | Notes |
| --- | --- | --- |
| ESP32-S3 | Main controller | The PlatformIO environment targets an ESP32-S3. |
| 240 x 240 round LCD and touch | User interface | The configured target is a Waveshare 1.28-inch touch LCD. |
| CAN | OBD-II data | The firmware uses the ESP32 TWAI controller. A suitable CAN transceiver is required. |
| GPIO 18 | Analogue boost input | The default configuration uses this input. |
| GPIO 17 | Reserved analogue input | The firmware prepares this input for future use. |
| QMI8658 IMU | G-meter data | The display reports an offline state if the IMU is unavailable. |

The CAN pin definitions are in `src/obd/obd.h`. The default analogue input
definitions are in `src/Sensors.cpp`. Check the source and your board revision
before you wire a device.

## Vehicle installation

Use an automotive-rated power supply design. Protect the device against fuse
failure, reverse polarity, load dump, noise, and poor ground connections.
Keep CAN wiring short where possible and follow the vehicle and transceiver
requirements for termination.

Do not rely on this display for safety-critical warnings. Do not change vehicle
wiring while the vehicle is moving.

## Planned additions

- PCB source files and gerbers.
- Bill of materials.
- Power and CAN wiring diagrams.
- Connector pinout.
- Enclosure and mounting guidance.
- Hardware revision and test matrix.
