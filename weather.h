#pragma once
#include <Arduino.h>

// พยากรณ์รายวัน
struct DayForecast {
  String date;          // YYYY-MM-DD
  int code = -1;
  float tmax = NAN, tmin = NAN;
  int rainChance = -1;  // %
};

// คุณภาพอากาศ (Open-Meteo Air Quality)
struct AirQuality {
  bool valid = false;
  float pm25 = NAN;     // µg/m³
  float pm10 = NAN;
  int usAqi = -1;
  float hPm[12];              // PM2.5 รายชั่วโมง 12 ชม. ข้างหน้า
  int hPmHour[12];
  int hPmCount = 0;
  String error;
};

// ข้อมูลสภาพอากาศปัจจุบัน (จาก Open-Meteo — ฟรี ไม่ต้องใช้ API key)
struct Weather {
  bool valid = false;
  String time;          // เวลาท้องถิ่นของข้อมูล เช่น 2026-11-05T17:45
  String timezone;
  long utcOffset = 0;         // วินาที จากเวลา UTC (ใช้ตั้งนาฬิกา)
  float temp = NAN;           // °C
  float feelsLike = NAN;      // °C
  int humidity = -1;          // %
  float precipitation = NAN;  // mm
  int weatherCode = -1;       // รหัส WMO
  int cloudCover = -1;        // %
  float pressure = NAN;       // hPa
  float windSpeed = NAN;      // km/h
  int windDir = -1;           // องศา
  bool isDay = true;
  float tempMax = NAN, tempMin = NAN;
  int rainChance = -1;        // % โอกาสฝนตกวันนี้
  float uvMax = NAN;
  String sunrise, sunset;
  DayForecast days[3];        // พรุ่งนี้ + อีก 2 วัน
  int rainNext2h = -1;        // % โอกาสฝนสูงสุดใน 2 ชั่วโมงข้างหน้า
  // รายชั่วโมง 24 ชม. ข้างหน้า (เริ่มจากชั่วโมงปัจจุบัน)
  int hCount = 0;
  int hHour[24];
  float hTemp[24];
  int hRain[24];
  float hUv[24];
  unsigned long fetchedAt = 0;  // millis() ตอนดึงข้อมูล
  String error;
};

namespace WeatherApi {
// ดึงสภาพอากาศปัจจุบันตามพิกัด
bool fetch(double lat, double lon, Weather& out);
// ดึงค่าฝุ่น PM2.5 / PM10 / AQI
bool fetchAir(double lat, double lon, AirQuality& out);
// ระดับคุณภาพอากาศตามเกณฑ์ PM2.5 ของกรมควบคุมมลพิษ: 0 ดีมาก .. 4 มีผลกระทบต่อสุขภาพ, -1 ไม่ทราบ
int pm25Level(float pm25);
// ดัชนีความร้อน (เกณฑ์กรมอนามัย จากอุณหภูมิที่รู้สึก): 0 ปกติ 1 เฝ้าระวัง 2 เตือนภัย 3 อันตราย 4 อันตรายมาก
int heatLevel(float feelsLike);
// ระดับ UV (WHO): 0 ต่ำ 1 ปานกลาง 2 สูง 3 สูงมาก 4 อันตราย
int uvLevel(float uv);
const char* pm25LevelName(int level);
// วันในสัปดาห์จาก "YYYY-MM-DD" (0 = อาทิตย์), -1 ถ้าอ่านไม่ได้
int weekday(const String& date);
// ค้นหาพิกัดจากชื่อเมือง (ไทย/อังกฤษ) — ได้ชื่อที่แสดงผลกลับมาใน displayName
bool geocode(const String& city, double& lat, double& lon, String& displayName);
// คำอธิบายสภาพอากาศภาษาไทย / อีโมจิ จากรหัส WMO
const char* describe(int code);
const char* icon(int code, bool isDay);
const char* windDirection(int deg);
}
