#pragma once
#include <Arduino.h>

// อัปเดตเฟิร์มแวร์อัตโนมัติผ่าน Wi-Fi จากเว็บติดตั้ง (GitHub Pages)
// ตรวจ version.json หลังเปิดเครื่อง และทุก ๆ AUTO_UPDATE_EVERY_MS — ถ้ามีเวอร์ชันใหม่ จะดาวน์โหลด ติดตั้ง แล้วรีสตาร์ทเอง
namespace AutoUpdate {
void loop();                 // เรียกใน loop() หลังต่อ Wi-Fi ได้
bool checkNow(bool verbose); // ตรวจทันที (คำสั่ง Serial: update)
}
