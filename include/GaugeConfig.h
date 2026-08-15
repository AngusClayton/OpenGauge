#pragma once

#include <stdint.h>

enum GaugeType {
    GAUGE_TYPE_STANDARD,
    GAUGE_TYPE_GMETER,
    GAUGE_TYPE_SHIFTLIGHT,
    GAUGE_TYPE_ACCEL_TIMER
};

struct SecondaryMetric {
    char sourceId[16];
    char prefix[16];
    char suffix[8];
    int posY;
    bool dynamicColor;
};

struct GaugeConfig {
    GaugeType type;
    char name[32];
    
    char mainSourceId[16];
    float minVal;
    float maxVal;
    char unitLabel[16];
    
    SecondaryMetric secondaries[3];
    uint8_t secondaryCount;
    bool boostUnits;
};
