#pragma once

#include "ConfigManager.h"
#include <stddef.h>
#include <stdint.h>

constexpr uint32_t OPEN_GAUGE_CONFIG_VERSION = 1;
constexpr size_t OPEN_GAUGE_MAX_CONFIG_BYTES = 32768;

struct ParsedOpenGaugeConfig {
  DataSourceConfig dataSources[MAX_DATA_SOURCES];
  size_t dataSourceCount;
  GaugeConfig gauges[MAX_GAUGES];
  size_t gaugeCount;
};

bool parseOpenGaugeConfig(const char* json, size_t length,
                          ParsedOpenGaugeConfig& output,
                          char* error, size_t errorSize);

const char* getDefaultOpenGaugeConfigJson();
