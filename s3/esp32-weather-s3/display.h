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
void showWifiFailedReason(int wlStatus);   // บอกสาเหตุที่ต่อ Wi-Fi ไม่ได้ (รหัสผิด / ไม่พบ ฯลฯ)
// ตั้งค่า Wi-Fi บนจอสัมผัส: เลือกเครือข่าย + แป้นพิมพ์บนจอ
void beginWifiSetup(const char* apSsid, const String& apIp);
bool wifiSetupLoop(String& ssid, String& pass);  // คืน true เมื่อผู้ใช้กด "เชื่อมต่อ"
void wifiSetupBackToKeyboard();
bool wifiSetupBusy();
void setIdleHook(void (*hook)());          // งานที่ต้องทำต่อระหว่างรอแตะ (เว็บ, Improv)                       // ผู้ใช้กำลังตั้งค่าอยู่ (อย่ารีสตาร์ท)
void showPortal(const char* apSsid, const String& ip);
void showLoading();
void showUpdating();                       // แถบเล็ก ๆ ระหว่างอัปเดต
void showUpdatingFirmware(const String& version);  // หน้าจอระหว่างอัปเดตเฟิร์มแวร์อัตโนมัติ
// 5 หน้า: นาฬิกา / สรุปวันนี้ / แผนที่ฝน / อากาศตอนนี้ / พยากรณ์
void showWeather(const Settings& s, const Weather& w, const AirQuality& a, const Market& m);
void refresh();                            // วาดหน้าปัจจุบันใหม่ (เช่น เมื่อข้อมูลตลาดอัปเดต)
void nextPage();                           // แตะจอ = เปลี่ยนหน้า
void beep();                               // เสียงเตือนสั้น ๆ ผ่านลำโพงบนบอร์ด
void showDisplayMode(uint8_t mode);
void setRgb(bool r, bool g, bool b);       // ไฟ LED RGB บนบอร์ด
Touch loop();
// โหมดภาพ = ชุดคำสั่งเริ่มจอ (แก้จอซีด/ไม่สว่าง) แตะค้าง 5 วินาทีเพื่อเลือกใหม่
bool needsPresetChoice();
void choosePreset();
void setPreset(uint8_t p);
uint8_t preset();
// ใช้ตอนทดสอบลำโพง
bool waitTap(unsigned long ms);
bool hasTouch();
void showSoundTest(int set, int total);
void showSoundOk();                              // อ่านการแตะจอ + อัปเดตนาฬิกา

}  // namespace Display
