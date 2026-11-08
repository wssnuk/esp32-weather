#pragma once
#include <Arduino.h>

// ใช้ HTTPS ทีละงาน (task เรดาร์ + loop หลัก) เพื่อไม่ให้หน่วยความจำภายในไม่พอจนบอร์ดรีสตาร์ท
// และบันทึก "ขั้นตอนล่าสุด" ไว้ในหน่วยความจำ RTC (ไม่หายตอนรีสตาร์ท) เพื่อบอกสาเหตุถ้าบอร์ดค้าง
namespace Net {
void lock();
void unlock();
struct Guard {
  Guard() { lock(); }
  ~Guard() { unlock(); }
};
}  // namespace Net

enum Stage : uint8_t {
  ST_BOOT = 1, ST_LOOP, ST_WEATHER, ST_AIR, ST_MARKET, ST_GEO, ST_UPDATE, ST_RENDER,
  ST_RADAR_NET, ST_RADAR_PNG, ST_RADAR_MIX, ST_RADAR_IDLE, ST_SOUND
};
extern volatile uint8_t g_stage;       // ขั้นตอนของ loop หลัก
extern volatile uint8_t g_radarStage;  // ขั้นตอนของ task เรดาร์
const char* stageName(uint8_t s);
