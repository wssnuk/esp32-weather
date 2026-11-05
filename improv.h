#pragma once
#include <Arduino.h>

// Improv Wi-Fi (Serial) — ให้หน้าเว็บติดตั้ง (ESP Web Tools) ตั้งค่า Wi-Fi ผ่านสาย USB ได้
// https://www.improv-wifi.com/serial/
namespace Improv {
// ป้อนทีละไบต์จาก Serial; คืน true ถ้าไบต์นี้เป็นส่วนของแพ็กเก็ต Improv (ไม่ต้องส่งต่อให้ตัวรับคำสั่ง)
// ไบต์ที่ไม่ใช่ Improv จะถูกส่งกลับผ่าน passthrough
bool feed(uint8_t b, void (*passthrough)(char));
}
