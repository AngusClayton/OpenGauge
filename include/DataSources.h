#pragma once

#include <stdint.h>

enum DataSourceType {
    SOURCE_OBD,
    SOURCE_ANALOG
};

struct DataSourceConfig {
    char id[16];
    DataSourceType type;
    
    // For OBD
    uint8_t pid;
    uint8_t formula; // Maps to PidFormula
    
    // GPIO for analog; multiplier/offset apply to either source type.
    uint8_t pin;
    float multiplier;
    float offset;
    
    float cachedValue;
};

// Raw OBD response packed as A in the high byte and B in the low byte.
inline float evaluateObdSource(const DataSourceConfig& source, uint16_t raw) {
    const float a = raw >> 8;
    float value = 0;
    switch (source.formula) {
        case 0: case 6: value = a; break;
        case 1: case 7: value = raw; break;
        case 2: value = a - 40; break;
        case 3: value = raw / 4.0f; break;
        case 4: value = raw / 100.0f; break;
        case 5: value = a / 2.55f; break;
    }
    return value * source.multiplier + source.offset;
}
