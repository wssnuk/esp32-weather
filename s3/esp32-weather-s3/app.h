#pragma once
#include "settings.h"
#include "weather.h"
#include "market.h"

// สถานะกลางของโปรแกรม (อยู่ใน esp32-weather.ino)
extern Settings settings;
extern Weather weather;
extern AirQuality air;
extern Market market;

void requestWeatherRefresh();
bool changeCity(const String& name, String& message);
void autoLocation(bool force);  // หาตำแหน่งจากอินเทอร์เน็ต + ชื่อตำบล/อำเภอ/จังหวัด
bool setExactLocation(double lat, double lon, const String& name, String& message);  // พิกัดจริงจาก GPS มือถือ
void resetWifiAndRestart();
