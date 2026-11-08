#pragma once
#include <Arduino.h>

// เรดาร์ฝนจริง (RainViewer — รวมเรดาร์กรมอุตุนิยมวิทยา) บนแผนที่ Esri ตามพิกัดของบอร์ด
// บอร์ดดาวน์โหลดและประกอบภาพเองใน task แยก (ไม่ทำให้นาฬิกาสะดุด)
namespace lgfx { inline namespace v1 { class LovyanGFX; } }

constexpr int RADAR_W = 320, RADAR_H = 240;   // เต็มจอ
constexpr int RADAR_FRAMES = 6;               // ย้อนหลัง 1 ชั่วโมง (ทุก 10 นาที)

struct RadarInfo {
  bool ready = false;          // มีภาพอย่างน้อย 1 เฟรม
  bool covered = true;         // พื้นที่นี้มีเรดาร์ครอบคลุม
  int frames = 0;
  uint32_t frameTime[RADAR_FRAMES] = {};  // epoch ของแต่ละเฟรม (เก่า → ใหม่)
  float kmPerPx = 0.6f;
  // วิเคราะห์จากเฟรมล่าสุด
  float hereMmh = 0;           // ความแรงฝนตรงตำแหน่งนี้ (มม./ชม.)
  float nearestKm = NAN;       // กลุ่มฝนที่ใกล้ที่สุด (≥ ฝนเบา) ในรัศมี 80 กม.
  int bearing = -1;            // ทิศของกลุ่มฝน (องศา 0 = เหนือ)
  float maxMmh = 0;            // ฝนแรงสุดในรัศมี 30 กม.
  bool approaching = false;    // กลุ่มฝนเคลื่อนเข้าหา
  float speedKmh = NAN;
  int etaMin = -1;             // คาดว่าจะถึงในกี่นาที
  uint32_t updatedAt = 0;      // epoch ที่ตรวจล่าสุด
  uint8_t phase = 0;           // 0 พัก, 1 โหลดแผนที่, 2 โหลดภาพเรดาร์
  uint8_t progress = 0;        // กำลังโหลดภาพที่เท่าไหร่
  String error;
};

namespace Radar {
void begin();
bool started();                             // เริ่ม task แล้วหรือยัง (โหมดปลอดภัยจะยังไม่เริ่ม)
void setLocation(double lat, double lon);   // เปลี่ยนตำแหน่ง → โหลดแผนที่ใหม่
RadarInfo info();                           // สำเนาข้อมูลล่าสุด
bool drawFrame(int i, lgfx::LovyanGFX* dst); // วาดเฟรม i (0 = เก่าสุด) เต็มจอ
}
