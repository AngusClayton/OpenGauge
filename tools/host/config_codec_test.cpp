#include "ConfigCodec.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <cmath>

int main() {
  ParsedOpenGaugeConfig config{};
  char error[192]{};
  const char* defaults = getDefaultOpenGaugeConfigJson();
  assert(parseOpenGaugeConfig(defaults, std::strlen(defaults), config, error, sizeof(error)));
  assert(config.dataSourceCount == 10);
  auto value = [&](const char* id, uint16_t raw) {
    for (size_t i=0; i<config.dataSourceCount; ++i)
      if (std::strcmp(config.dataSources[i].id,id)==0) return evaluateObdSource(config.dataSources[i],raw);
    assert(false); return 0.0f;
  };
  assert(std::fabs(value("lambda",32768)-1.0f)<0.0001f);
  assert(std::fabs(value("afr",32768)-14.7f)<0.0001f);
  assert(std::fabs(value("maf",8000)-80.0f)<0.0001f);
  assert(std::fabs(value("hp",8000)-100.0f)<0.0001f);
  assert(std::fabs(value("ignition",0xA000)-16.0f)<0.0001f);
  assert(std::strcmp(config.gauges[1].mainSourceId,"hp")==0);
  assert(std::strcmp(config.gauges[1].secondaries[0].sourceId,"maf")==0);
  assert(config.gaugeCount == 7);
  assert(config.gauges[4].shiftTargets[0] == 6500);

  // Explicit conversion must survive a source rename and shared-PID use.
  std::string renamed(defaults);
  size_t pos = 0;
  while ((pos = renamed.find("\"afr\"", pos)) != std::string::npos) {
    renamed.replace(pos, 5, "\"fuelRatio\""); pos += 11;
  }
  assert(parseOpenGaugeConfig(renamed.c_str(), renamed.size(), config, error, sizeof(error)));
  assert(std::fabs(value("fuelRatio",26214)-11.75982f)<0.001f);
  assert(std::fabs(value("lambda",26214)-0.799988f)<0.0001f);

  const char* legacy = R"({"version":1,"dataSources":[{"id":"afr","type":"obd","pid":52,"formula":7},{"id":"lambda","type":"obd","pid":52,"formula":7}],"gauges":[{"name":"AFR","type":"standard","mainSourceId":"afr","minVal":10,"maxVal":20}]})";
  assert(parseOpenGaugeConfig(legacy, std::strlen(legacy), config, error, sizeof(error)));
  assert(std::fabs(value("afr",32768)-14.7f)<0.0001f);
  assert(std::fabs(value("lambda",32768)-1.0f)<0.0001f);

  std::string invalid(defaults);
  invalid.replace(invalid.find("\"version\": 1"), std::strlen("\"version\": 1"), "\"version\": 99");
  assert(!parseOpenGaugeConfig(invalid.c_str(), invalid.size(), config, error, sizeof(error)));
  std::cout << "Config codec tests passed\n";
}
