#include "ConfigCodec.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
  ParsedOpenGaugeConfig config{};
  char error[192]{};
  const char* defaults = getDefaultOpenGaugeConfigJson();
  assert(parseOpenGaugeConfig(defaults, std::strlen(defaults), config, error, sizeof(error)));
  assert(config.dataSourceCount == 9);
  assert(config.gaugeCount == 7);
  assert(config.gauges[4].shiftTargets[0] == 6500);

  std::string invalid(defaults);
  invalid.replace(invalid.find("\"version\": 1"), std::strlen("\"version\": 1"), "\"version\": 99");
  assert(!parseOpenGaugeConfig(invalid.c_str(), invalid.size(), config, error, sizeof(error)));
  std::cout << "Config codec tests passed\n";
}
