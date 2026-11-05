#pragma once
#include <Arduino.h>

// การตั้งค่าที่บันทึกถาวรในหน่วยความจำ Flash (ไม่หายเมื่อปิดไฟ)
struct Settings {
  String wifiSsid;
  String wifiPass;
  String city;
  double lat;
  double lon;
  uint8_t displayMode;
};

namespace Store {
void load(Settings& s);
void saveWifi(const String& ssid, const String& pass);
void saveLocation(const String& city, double lat, double lon);
void clearWifi();
void saveDisplayMode(uint8_t mode);
}
