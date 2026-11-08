#pragma once
#include <Arduino.h>

// แผนที่ฝน (เรดาร์/แบบจำลอง) — ไฟล์ที่ GitHub Actions สร้างทุก 10 นาที (ดู server/radar.py)
// ดาวน์โหลดใน task แยก เพื่อไม่ให้นาฬิกาสะดุด
struct RadarData {
  bool valid = false;
  bool rainingHere = false, approaching = false;
  float nearestKm = NAN, speedKmh = NAN, kmPerPx = 0.45f;
  int bearing = -1, etaMin = -1;
  bool model = true;              // true = แบบจำลองฝน (Open-Meteo), false = เรดาร์จริง
  uint32_t generated = 0;
  double lat = NAN, lon = NAN;
  int w = 224, h = 204;
  uint32_t frames[4] = {};
  uint8_t* jpg[4] = {};
  size_t len[4] = {};
  unsigned long fetchedMs = 0;
};

namespace Radar {
void begin();                     // เริ่ม task ดาวน์โหลด (เรียกหลังต่อ Wi-Fi แล้ว)
const RadarData* get();           // ข้อมูลล่าสุด (nullptr ถ้ายังไม่มี)
bool usableAt(double lat, double lon);  // แผนที่ครอบคลุมเมืองที่ตั้งไว้ไหม
}
