#pragma once
#include <Arduino.h>
#include "settings.h"
#include "weather.h"
#include "market.h"

// จอสัมผัส 2.8" บอร์ด ESP32-S3 (ILI9341 แบบ SPI + ทัช capacitive) — ธีม Aurora Gold
namespace Display {

enum class Touch { None, Tap, LongPress };

constexpr uint8_t MODE_COUNT = 4;   // ทิศการหมุนจอ (เผื่อบอร์ดบางล็อตติดจอกลับด้าน)
const char* modeName(uint8_t mode);

void begin(uint8_t mode);
void showBoot(const char* version);
void showConnecting(const String& ssid);
void showWifiFailed();
void showPortal(const char* apSsid, const String& ip);
void showLoading();
void showUpdating();                       // แถบเล็ก ๆ ระหว่างอัปเดต
void showUpdatingFirmware(const String& version);  // หน้าจอระหว่างอัปเดตเฟิร์มแวร์อัตโนมัติ
// 3 หน้า: นาฬิกา (หน้าหลัก) / อากาศตอนนี้ / พยากรณ์
void showWeather(const Settings& s, const Weather& w, const AirQuality& a, const Market& m);
void refresh();                            // วาดหน้าปัจจุบันใหม่ (เช่น เมื่อข้อมูลตลาดอัปเดต)
void nextPage();                           // แตะจอ = เปลี่ยนหน้า
void beep();                               // เสียงเตือนสั้น ๆ ผ่านลำโพงบนบอร์ด
void showDisplayMode(uint8_t mode);
void setRgb(bool r, bool g, bool b);       // ไฟ LED RGB บนบอร์ด
Touch loop();                              // อ่านการแตะจอ + อัปเดตนาฬิกา

}  // namespace Display
