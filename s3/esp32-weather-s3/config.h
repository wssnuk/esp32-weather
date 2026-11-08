// ===== ตั้งค่าหลักของระบบ — แก้ได้ที่ไฟล์นี้ไฟล์เดียว =====
#pragma once

#define APP_NAME            "ESP32 Weather S3"
#define APP_VERSION         "1.2.0"
#define DEVICE_HOSTNAME     "esp32-weather-s3"        // เข้าเว็บได้ที่ http://esp32-weather.local

// เมืองเริ่มต้น (เปลี่ยนได้ภายหลังจากหน้าเว็บ หรือพิมพ์ใน Serial Monitor)
#define DEFAULT_CITY        "Bangkok"
#define DEFAULT_LAT         13.7563
#define DEFAULT_LON         100.5018

// Wi-Fi ตอนตั้งค่าครั้งแรก (บอร์ดจะปล่อย Wi-Fi ชื่อนี้ให้โทรศัพท์เชื่อมต่อ)
#define SETUP_AP_SSID       "ESP32-Weather-S3"
#define WIFI_CONNECT_TIMEOUT_MS   20000
#define PORTAL_RETRY_MS     180000   // ถ้ามีรหัส Wi-Fi แล้วแต่ต่อไม่ได้ เปิดหน้าตั้งค่า 3 นาทีแล้วลองใหม่

// รอบการอัปเดตข้อมูลอากาศ
#define WEATHER_UPDATE_MS   (5UL * 60UL * 1000UL)    // ทุก 5 นาที (เกือบเรียลไทม์)
#define RAIN_ALERT_PERCENT  60                       // เตือนเมื่อโอกาสฝนใน 2 ชม. ถึงค่านี้
#define BRIGHTNESS_DAY      255
#define BRIGHTNESS_NIGHT    140                      // ความสว่างตอนกลางคืน (0-255)
#define NIGHT_START_HOUR    22
#define NIGHT_END_HOUR      6
// ข้อมูลตลาด (ทอง/หุ้น) — ไฟล์ที่ GitHub Actions อัปเดตให้
#define MARKET_DATA_URL     "https://raw.githubusercontent.com/wssnuk/esp32-weather/market-data/market.json"
#define MARKET_UPDATE_MS    (5UL * 60UL * 1000UL)
// อัปเดตเฟิร์มแวร์อัตโนมัติ
#define AUTO_UPDATE_URL     "https://wssnuk.github.io/esp32-weather/s3w/version.json"
#define AUTO_UPDATE_EVERY_MS (6UL * 60UL * 60UL * 1000UL)   // ทุก 6 ชั่วโมง
#define WEATHER_RETRY_MS    (30UL * 1000UL)          // ถ้าดึงไม่สำเร็จ ลองใหม่ใน 30 วินาที

// ขา GPIO
// บอร์ด ESP32-2432S028 (Cheap Yellow Display) — ขาจอ/ทัช/LED อยู่ใน display.cpp
#define PIN_BUTTON          0        // ปุ่ม BOOT — กดค้าง 3 วินาทีเพื่อล้างการตั้งค่า Wi-Fi
#define DEFAULT_DISPLAY_MODE 0       // S3: โหมดจอ = ทิศการหมุนจอ (0-3) แตะค้าง 5 วินาทีเพื่อเปลี่ยน
