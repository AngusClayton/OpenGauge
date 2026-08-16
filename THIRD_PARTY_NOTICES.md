# Third-party notices

OpenGauge includes and depends on third-party software. Each component remains
under its own licence. Retain the copyright and licence notices when you copy
or distribute these files.

## Files included in the repository

- `src/Display/CST816S.cpp` and `src/Display/CST816S.h`: CST816S touch driver,
  copyright Felix Biego, MIT License. The complete notice is in each file.
- `src/Display/DEV_Config.cpp` and `src/Display/DEV_Config.h`: Waveshare device
  support code. The complete permissive licence notice is in each file.
- `src/Display/font8.cpp`, `font12.cpp`, `font16.cpp`, `font20.cpp`,
  `font24.cpp`, and `fonts.h`: font tables derived from STMicroelectronics
  material. The complete redistribution notice is in each file.

## Build dependencies

PlatformIO downloads the following libraries during a firmware build:

- ESP32-TWAI-CAN 1.0.1
- cJSON 1.7.18
- ArduinoJson 7.4.3
- the Espressif Arduino framework and its transitive components

Consult each installed package for its licence and source notices. These
packages are not relicensed by OpenGauge.
