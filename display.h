#pragma once
#include <Arduino.h>
#include "settings.h"
#include "weather.h"

// จอสัมผัส 2.8" ของบอร์ด ESP32-2432S028 (Cheap Yellow Display)
namespace Display {

enum class Touch { None, Tap, LongPress };

constexpr uint8_t MODE_COUNT = 6;   // ไดรเวอร์จอ × กลับสี (บอร์ดแต่ละล็อตใช้จอต่างกัน)
const char* modeName(uint8_t mode);

void begin(uint8_t mode);
void showBoot(const char* version);
void showConnecting(const String& ssid);
void showWifiFailed();
void showPortal(const char* apSsid, const String& ip);
void showLoading();
void showUpdating();                       // แถบเล็ก ๆ ระหว่างอัปเดต
void showWeather(const Settings& s, const Weather& w, const String& ip);
void showDisplayMode(uint8_t mode);
void setRgb(bool r, bool g, bool b);       // ไฟ LED RGB บนบอร์ด
Touch loop();                              // อ่านการแตะจอ + อัปเดตนาฬิกา

}  // namespace Display
