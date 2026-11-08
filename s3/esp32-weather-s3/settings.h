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
  String addrTh;        // ที่อยู่ไทยแบบย่อ เช่น "ต.ประตูชัย อ.พระนครศรีอยุธยา จ.พระนครศรีอยุธยา"
  bool locManual;       // true = ผู้ใช้ตั้งเอง (GPS มือถือ/ชื่อเมือง), false = หาอัตโนมัติจากอินเทอร์เน็ต
};

namespace Store {
void load(Settings& s);
void saveWifi(const String& ssid, const String& pass);
void saveLocation(const String& city, double lat, double lon);
void clearWifi();
void saveDisplayMode(uint8_t mode);
void saveAddress(const String& addrTh);
void saveLocManual(bool manual);
}
