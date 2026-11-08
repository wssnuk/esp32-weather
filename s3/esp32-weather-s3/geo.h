#pragma once
#include <Arduino.h>

// หาตำแหน่งของบอร์ดผ่านอินเทอร์เน็ต (ไม่มีชิป GPS):
//  1) สแกน Wi-Fi รอบตัว แล้วถาม BeaconDB (ฐานข้อมูลตำแหน่ง Wi-Fi แบบเปิด) — ถ้าไม่มีข้อมูลจะใช้ IP แทน
//  2) แปลงพิกัดเป็นที่อยู่ไทย ตำบล/อำเภอ/จังหวัด (OpenStreetMap Nominatim, สำรอง BigDataCloud)
namespace Geo {

struct Fix {
  double lat = NAN, lon = NAN;
  float accuracyM = 0;   // ความคลาดเคลื่อนโดยประมาณ (เมตร)
  bool fromWifi = false; // true = จากสัญญาณ Wi-Fi รอบตัว, false = จากหมายเลข IP (หยาบกว่า)
};

struct Address {
  String sub, district, province;  // ภาษาไทยเต็ม เช่น ตำบลประตูชัย / อำเภอพระนครศรีอยุธยา / จังหวัดพระนครศรีอยุธยา
  String latinName;                // ชื่อภาษาอังกฤษสั้น ๆ (ใช้กับหน้าเว็บ/Serial)
};

bool locate(Fix& out, String& err);
bool reverse(double lat, double lon, Address& out, String& err);
String shortThai(const Address& a);  // "ต.ประตูชัย อ.พระนครศรีอยุธยา จ.พระนครศรีอยุธยา"

}  // namespace Geo
