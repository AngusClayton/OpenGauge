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
    
    // For Analog
    uint8_t pin;
    float multiplier;
    float offset;
    
    float cachedValue;
};
