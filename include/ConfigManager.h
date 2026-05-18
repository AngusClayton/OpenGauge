#pragma once

#include "DataSources.h"
#include "GaugeConfig.h"
#include <stddef.h>

constexpr size_t MAX_DATA_SOURCES = 10;
constexpr size_t MAX_GAUGES = 10;

extern DataSourceConfig activeDataSources[MAX_DATA_SOURCES];
extern size_t activeDataSourceCount;

extern GaugeConfig activeGauges[MAX_GAUGES];
extern size_t activeGaugeCount;

void loadConfigFromJson();
float getValueForSource(const char* sourceId);
