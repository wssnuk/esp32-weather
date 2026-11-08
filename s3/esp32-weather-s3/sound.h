#pragma once
#include <Arduino.h>

// ลำโพงบนบอร์ด (ชิปเสียง ES8311 + I2S) — เสียงสังเคราะห์ที่แต่งขึ้นเอง ไม่มีเพลงลิขสิทธิ์
namespace Sound {

enum Melody : uint8_t { MEL_TEST, MEL_BOOT, MEL_RAIN, MEL_HOUR, MEL_OK };

bool begin();          // ตรวจหาชิปเสียง + โหลดชุดขาที่ทดสอบแล้ว
bool needsTest();      // ยังไม่เคยทดสอบลำโพง
void runTest();        // ทดสอบลำโพง: เล่นทีละชุด ผู้ใช้แตะจอเมื่อได้ยิน
bool ready();          // ใช้งานเสียงได้
void play(Melody m);   // เล่นแบบไม่บล็อก
void loop();           // เสียงบอกเวลาทุกชั่วโมง (07:00-21:00)
String status();

}  // namespace Sound
