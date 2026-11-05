#pragma once
#include <Arduino.h>

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
  unsigned long fetchedAt = 0;  // millis() ตอนดึงข้อมูล
  String error;
};

namespace WeatherApi {
// ดึงสภาพอากาศปัจจุบันตามพิกัด
bool fetch(double lat, double lon, Weather& out);
// ค้นหาพิกัดจากชื่อเมือง (ไทย/อังกฤษ) — ได้ชื่อที่แสดงผลกลับมาใน displayName
bool geocode(const String& city, double& lat, double& lon, String& displayName);
// คำอธิบายสภาพอากาศภาษาไทย / อีโมจิ จากรหัส WMO
const char* describe(int code);
const char* icon(int code, bool isDay);
const char* windDirection(int deg);
}
