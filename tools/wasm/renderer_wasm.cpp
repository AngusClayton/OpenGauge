#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <emscripten/emscripten.h>
#include "ConfigCodec.h"
#include "ConfigManager.h"
#include "GaugeRenderer.h"
#include "Sensors.h"
#include "Display/GUI_Paint.h"
#include "Display/LCD_1in28.h"
#include "obd/obd.h"
#include "obd/pid_config.h"

void renderGenericGauge(const GaugeConfig& config);
void renderShiftLightGauge();
void renderGmeterGauge();
void renderAccelerationTimerGauge();

struct SampleValue { char id[16]; float value; };
static SampleValue samples[24]{};
static size_t sampleCount = 0;
static uint32_t previewMillis = 10000;
static float lateralG = 0, longitudinalG = 0;
static bool imuReady = true;
static GForcePeak peaks[kGforcePeakBufferSize]{};
static char lastError[192]{};
static UWORD framebuffer[240 * 240]{};

UWORD* BlackImage = framebuffer;
OBDValues obdValues{};
GaugeConfig activeGauges[MAX_GAUGES]{};
size_t activeGaugeCount = 0;
DataSourceConfig activeDataSources[MAX_DATA_SOURCES]{};
size_t activeDataSourceCount = 0;
LCD_1IN28_ATTRIBUTES LCD_1IN28{240, 240, 0};
static size_t currentGaugeIndex = 0;

uint32_t millis() { return previewMillis; }
float getValueForSource(const char* id) { for (size_t i=0;i<sampleCount;++i) if(strcmp(samples[i].id,id)==0) return samples[i].value; return 0.0f; }
size_t getCurrentGaugeProfileIndex() { return currentGaugeIndex; }
void lockConfig() {}
void unlockConfig() {}
OBDLinkStatus getOBDLinkStatus() { return OBD_STATUS_READY; }
const char* getOBDStatusText() { return "READY"; }
bool isImuReady() { return imuReady; }
float getLateralG() { return lateralG; }
float getLongitudinalG() { return longitudinalG; }
const GForcePeak* getGforcePeakBuffer() { return peaks; }
void LCD_1IN28_Display(UWORD*) {}

static float sample(const char* id, float fallback=0.0f) { for(size_t i=0;i<sampleCount;++i) if(strcmp(samples[i].id,id)==0) return samples[i].value; return fallback; }

extern "C" {
EMSCRIPTEN_KEEPALIVE uint32_t og_renderer_abi_version() { return 1; }
EMSCRIPTEN_KEEPALIVE uintptr_t og_framebuffer_ptr() { return reinterpret_cast<uintptr_t>(framebuffer); }
EMSCRIPTEN_KEEPALIVE uint32_t og_framebuffer_size() { return sizeof(framebuffer); }
EMSCRIPTEN_KEEPALIVE const char* og_last_error() { return lastError; }

EMSCRIPTEN_KEEPALIVE int og_render(const char* configJson, int configLength, int gaugeIndex,
                                   const char* samplesJson, int samplesLength, uint32_t timestampMs) {
  ParsedOpenGaugeConfig parsed{};
  if (!parseOpenGaugeConfig(configJson, (size_t)configLength, parsed, lastError, sizeof(lastError))) return 1;
  if (gaugeIndex < 0 || (size_t)gaugeIndex >= parsed.gaugeCount) { snprintf(lastError,sizeof(lastError),"Select a valid gauge."); return 2; }
  JsonDocument values;
  if (deserializeJson(values, samplesJson, (size_t)samplesLength)) { snprintf(lastError,sizeof(lastError),"Sample values are not valid JSON."); return 3; }
  sampleCount=0;
  for (JsonPair pair : values.as<JsonObject>()) {
    if(sampleCount>=24 || !pair.value().is<float>()) continue;
    snprintf(samples[sampleCount].id,sizeof(samples[sampleCount].id),"%s",pair.key().c_str());
    samples[sampleCount].value=pair.value().as<float>(); ++sampleCount;
  }
  memcpy(activeDataSources,parsed.dataSources,sizeof(activeDataSources)); activeDataSourceCount=parsed.dataSourceCount;
  memcpy(activeGauges,parsed.gauges,sizeof(activeGauges)); activeGaugeCount=parsed.gaugeCount; currentGaugeIndex=(size_t)gaugeIndex;
  if(activeGauges[currentGaugeIndex].type==GAUGE_TYPE_SHIFTLIGHT) for(uint8_t gear=1;gear<=6;++gear) setShiftTargetRpm(gear,activeGauges[currentGaugeIndex].shiftTargets[gear-1]);
  previewMillis=timestampMs; lateralG=sample("lateralG",-0.42f); longitudinalG=sample("longitudinalG",0.18f); imuReady=sample("imuReady",1.0f)!=0.0f;
  for(size_t i=0;i<kGforcePeakBufferSize;++i){float fade=1.0f-(float)i/60.0f;peaks[i]={lateralG*fade+0.12f*sinf((float)i*0.45f),longitudinalG*fade+0.08f*cosf((float)i*0.45f),previewMillis-(uint32_t)(i*100)};}
  peaks[kGforcePeakBufferSize/2]={sample("peakLat",lateralG),sample("peakLong",longitudinalG),previewMillis-2500};
  obdValues.rpm=(uint16_t)std::clamp(sample("rpm"),0.0f,65535.0f); obdValues.vehicle_speed_kmh=(uint8_t)std::clamp(sample("speed"),0.0f,255.0f);
  Paint_NewImage((UBYTE*)BlackImage,240,240,ROTATE_0,BLACK); Paint_SetScale(65);
  const GaugeConfig& gauge=activeGauges[currentGaugeIndex];
  if(gauge.type==GAUGE_TYPE_STANDARD) renderGenericGauge(gauge);
  else if(gauge.type==GAUGE_TYPE_SHIFTLIGHT) renderShiftLightGauge();
  else if(gauge.type==GAUGE_TYPE_GMETER) renderGmeterGauge();
  else {
    const float finalSpeed=sample(gauge.mainSourceId,gauge.maxVal), elapsed=sample("timerMs",7420);
    resetAccelerationTimer();
    for(size_t i=0;i<sampleCount;++i) if(strcmp(samples[i].id,gauge.mainSourceId)==0) samples[i].value=gauge.minVal;
    previewMillis=0; renderAccelerationTimerGauge();
    for(size_t i=0;i<sampleCount;++i) if(strcmp(samples[i].id,gauge.mainSourceId)==0) samples[i].value=gauge.minVal+1;
    previewMillis=100; renderAccelerationTimerGauge();
    for(size_t i=0;i<sampleCount;++i) if(strcmp(samples[i].id,gauge.mainSourceId)==0) samples[i].value=finalSpeed;
    previewMillis+=(uint32_t)std::max(0.0f,elapsed); renderAccelerationTimerGauge();
  }
  lastError[0]='\0'; return 0;
}

#ifndef __EMSCRIPTEN__
int main() {
  const char* config = getDefaultOpenGaugeConfigJson();
  const char* values = "{\"boostPress\":12.4,\"waterTemp\":92,\"intakeTemp\":31}";
  const int result = og_render(config, (int)strlen(config), 0, values, (int)strlen(values), 10000);
  if (result != 0) { fprintf(stderr, "%s\n", og_last_error()); return result; }
  size_t nonBlack = 0; for (uint8_t byte : reinterpret_cast<uint8_t(&)[sizeof(framebuffer)]>(framebuffer)) if (byte) ++nonBlack;
  if (og_framebuffer_size() != 240 * 240 * 2 || nonBlack == 0) return 4;
  puts("Portable renderer smoke test passed"); return 0;
}
#endif
}
