#include <algorithm>
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
  std::puts("preview_host --type standard|shiftlight|gmeter [options] --raw output.rgb565\n"
            "  --value name=number  --main-source name  --min n  --max n  --unit text\n"
            "  --secondary source,prefix,suffix,y[,dynamic]  --boost-units");
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
    else if(a=="--value") { auto s=next(); auto p=s.find('='); if(p==std::string::npos) return 2; values[s.substr(0,p)]=std::stof(s.substr(p+1)); }
    else if(a=="--secondary") {
      std::string s=next(); size_t start=0; std::string fields[5]; int n=0;
      while(n<5) { auto p=s.find(',',start); fields[n++]=s.substr(start,p-start); if(p==std::string::npos) break; start=p+1; }
      if(n<4 || cfg.secondaryCount>=3) return 2;
      auto& sec=cfg.secondaries[cfg.secondaryCount++];
      std::snprintf(sec.sourceId,sizeof sec.sourceId,"%s",fields[0].c_str());
      std::snprintf(sec.prefix,sizeof sec.prefix,"%s",fields[1].c_str());
      std::snprintf(sec.suffix,sizeof sec.suffix,"%s",fields[2].c_str());
      sec.posY=std::stoi(fields[3]); sec.dynamicColor=n==5 && fields[4]=="dynamic";
    } else { usage(); return 2; }
  }
  values["lateralG"] = lateralG = values["lateralG"];
  values["longitudinalG"] = longitudinalG = values["longitudinalG"];
  obdValues.rpm=(uint16_t)std::clamp(values["rpm"],0.0f,65535.0f);
  obdValues.vehicle_speed_kmh=(uint8_t)std::clamp(values["speed"],0.0f,255.0f);
  cfg.type = type=="shiftlight"?GAUGE_TYPE_SHIFTLIGHT:type=="gmeter"?GAUGE_TYPE_GMETER:GAUGE_TYPE_STANDARD;
  std::snprintf(cfg.mainSourceId,sizeof cfg.mainSourceId,"%s",mainSource.c_str());
  std::snprintf(cfg.unitLabel,sizeof cfg.unitLabel,"%s",unit.c_str());
  cfg.minVal=minVal; cfg.maxVal=maxVal; cfg.boostUnits=boostUnits;
  Paint_NewImage((UBYTE*)BlackImage,240,240,ROTATE_0,BLACK); Paint_SetScale(65);
  if(type=="shiftlight") renderShiftLightGauge();
  else if(type=="gmeter") renderGmeterGauge();
  else renderGenericGauge(cfg);
  std::ofstream out(raw,std::ios::binary); out.write((char*)framebuffer,sizeof framebuffer);
  if(!out) { std::fprintf(stderr,"Cannot write %s\n",raw.c_str()); return 1; }
}
