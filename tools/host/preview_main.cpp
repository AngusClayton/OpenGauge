#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include "GaugeConfig.h"
#include "ConfigManager.h"
#include "Sensors.h"
#include "obd/obd.h"
#include "obd/pid_config.h"
#include "Display/GUI_Paint.h"
#include "Display/LCD_1in28.h"

// Production renderer entry points (kept private in the firmware header today).
void renderGenericGauge(const GaugeConfig& config);
void renderShiftLightGauge();
void renderGmeterGauge();
void renderAccelerationTimerGauge();
void setShiftTargetRpm(uint8_t gear, uint16_t targetRpm);

UWORD framebuffer[240 * 240];
UWORD* BlackImage = framebuffer;
OBDValues obdValues{};
GaugeConfig activeGauges[MAX_GAUGES]{};
size_t activeGaugeCount = 1;
DataSourceConfig activeDataSources[MAX_DATA_SOURCES]{};
size_t activeDataSourceCount = 0;
LCD_1IN28_ATTRIBUTES LCD_1IN28{240, 240, 0};

static std::map<std::string, float> values;
static uint32_t fakeMillis = 10000;
static float lateralG = 0, longitudinalG = 0;
static bool imuReady = true;
static GForcePeak peaks[kGforcePeakBufferSize]{};

uint32_t millis() { return fakeMillis; }
float getValueForSource(const char* id) { auto i=values.find(id); return i==values.end()?0.0f:i->second; }
size_t getCurrentGaugeProfileIndex() { return 0; }
OBDLinkStatus getOBDLinkStatus() { return OBD_STATUS_READY; }
const char* getOBDStatusText() { return "READY"; }
bool isImuReady() { return imuReady; }
float getLateralG() { return lateralG; }
float getLongitudinalG() { return longitudinalG; }
const GForcePeak* getGforcePeakBuffer() { return peaks; }
void LCD_1IN28_Display(UWORD*) {}

static void usage() {
  std::puts("preview_host --type standard|shiftlight|gmeter|accelTimer [options] --raw output.rgb565\n"
            "  --value name=number  --main-source name  --min n  --max n  --unit text\n"
            "  --secondary source,prefix,suffix[,range,low,high,below,between,above]  --shift-targets r1,r2,r3,r4,r5,r6\n"
            "  --boost-units");
}

int main(int argc, char** argv) {
  std::string type="standard", raw="preview.rgb565";
  std::string mainSource="value", unit;
  float minVal=0, maxVal=100;
  bool boostUnits=false;
  GaugeConfig& cfg=activeGauges[0];
  std::strcpy(cfg.name,"Host preview");
  for (int i=1;i<argc;i++) {
    std::string a=argv[i];
    auto next=[&]() -> std::string { if (++i>=argc) { usage(); std::exit(2); } return argv[i]; };
    if(a=="--type") type=next();
    else if(a=="--raw") raw=next();
    else if(a=="--main-source") mainSource=next();
    else if(a=="--min") minVal=std::stof(next());
    else if(a=="--max") maxVal=std::stof(next());
    else if(a=="--unit") unit=next();
    else if(a=="--boost-units") boostUnits=true;
    else if(a=="--offline") imuReady=false;
    else if(a=="--shift-targets") {
      std::string targets=next(); size_t start=0;
      for (uint8_t gear=1; gear<=6; gear++) {
        const size_t comma=targets.find(',', start);
        const std::string value=targets.substr(start, comma-start);
        if (value.empty()) return 2;
        setShiftTargetRpm(gear, (uint16_t)std::stoul(value));
        if (comma == std::string::npos && gear != 6) return 2;
        start=comma + 1;
      }
    }
    else if(a=="--value") { auto s=next(); auto p=s.find('='); if(p==std::string::npos) return 2; values[s.substr(0,p)]=std::stof(s.substr(p+1)); }
    else if(a=="--secondary") {
      std::string s=next(); size_t start=0; std::string fields[9]; int n=0;
      while(n<9) { auto p=s.find(',',start); fields[n++]=s.substr(start,p-start); if(p==std::string::npos) break; start=p+1; }
      if(n<3 || cfg.secondaryCount>=3) return 2;
      auto& sec=cfg.secondaries[cfg.secondaryCount++];
      std::snprintf(sec.sourceId,sizeof sec.sourceId,"%s",fields[0].c_str());
      std::snprintf(sec.prefix,sizeof sec.prefix,"%s",fields[1].c_str());
      std::snprintf(sec.suffix,sizeof sec.suffix,"%s",fields[2].c_str());
      sec.rangeColors=n>=4 && fields[3]=="range";
      sec.lowerThreshold=n>=5 ? std::stof(fields[4]) : 0.0f;
      sec.upperThreshold=n>=6 ? std::stof(fields[5]) : 100.0f;
      std::snprintf(sec.colorBelow,sizeof sec.colorBelow,"%s",n>=7?fields[6].c_str():"blue");
      std::snprintf(sec.colorBetween,sizeof sec.colorBetween,"%s",n>=8?fields[7].c_str():"cyan");
      std::snprintf(sec.colorAbove,sizeof sec.colorAbove,"%s",n>=9?fields[8].c_str():"red");
    } else { usage(); return 2; }
  }
  values["lateralG"] = lateralG = values["lateralG"];
  values["longitudinalG"] = longitudinalG = values["longitudinalG"];
  // Preview a five-second trail. peakLat/peakLong default to the current
  // values when not supplied and represent a sample from the same window.
  const float previewPeakLat = values.count("peakLat") ? values["peakLat"] : lateralG;
  const float previewPeakLong = values.count("peakLong") ? values["peakLong"] : longitudinalG;
  for (size_t i = 0; i < 50; i++) {
    const float fade = 1.0f - (float)i / 60.0f;
    peaks[i] = {lateralG * fade + 0.12f * sinf((float)i * 0.45f),
                longitudinalG * fade + 0.08f * cosf((float)i * 0.45f),
                fakeMillis - (uint32_t)(i * 100)};
  }
  peaks[25] = {previewPeakLat, previewPeakLong, fakeMillis - 2500};
  obdValues.rpm=(uint16_t)std::clamp(values["rpm"],0.0f,65535.0f);
  obdValues.vehicle_speed_kmh=(uint8_t)std::clamp(values["speed"],0.0f,255.0f);
  cfg.type = type=="shiftlight" ? GAUGE_TYPE_SHIFTLIGHT :
             type=="gmeter" ? GAUGE_TYPE_GMETER :
             type=="accelTimer" ? GAUGE_TYPE_ACCEL_TIMER : GAUGE_TYPE_STANDARD;
  std::snprintf(cfg.mainSourceId,sizeof cfg.mainSourceId,"%s",mainSource.c_str());
  std::snprintf(cfg.unitLabel,sizeof cfg.unitLabel,"%s",unit.c_str());
  cfg.minVal=minVal; cfg.maxVal=maxVal; cfg.boostUnits=boostUnits;
  Paint_NewImage((UBYTE*)BlackImage,240,240,ROTATE_0,BLACK); Paint_SetScale(65);
  if(type=="shiftlight") renderShiftLightGauge();
  else if(type=="gmeter") renderGmeterGauge();
  else if(type=="accelTimer") {
    // A preview process has no prior display frames. Create one stopped frame,
    // then one moving frame, so the final frame shows a real timer state.
    const float finalSpeed = values[mainSource];
    const uint32_t previewElapsedMs = (uint32_t)std::max(0.0f, values["timerMs"]);
    fakeMillis = 0;
    values[mainSource] = minVal;
    renderAccelerationTimerGauge();
    fakeMillis = 100;
    values[mainSource] = minVal + 1.0f;
    renderAccelerationTimerGauge();
    fakeMillis += previewElapsedMs;
    values[mainSource] = finalSpeed;
    renderAccelerationTimerGauge();
  }
  else renderGenericGauge(cfg);
  std::ofstream out(raw,std::ios::binary); out.write((char*)framebuffer,sizeof framebuffer);
  if(!out) { std::fprintf(stderr,"Cannot write %s\n",raw.c_str()); return 1; }
}
