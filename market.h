#pragma once
#include <Arduino.h>

// ข้อมูลตลาด: ราคาทองไทย, ดัชนี SET/SET50, ทองคำโลก, ค่าเงิน
// ดึงจากไฟล์ market.json ที่ GitHub Actions อัปเดตทุก 10 นาที (ดู server/ และ .github/workflows/market.yml)
struct Market {
  bool valid = false;
  uint32_t ts = 0;               // เวลาที่เซิร์ฟเวอร์เก็บข้อมูล (epoch)
  // ทองคำ 96.5% (บาท)
  bool goldValid = false;
  float barBuy = NAN, barSell = NAN, ornBuy = NAN, ornSell = NAN, goldChange = NAN;
  String goldTime;               // HH:MM ตามที่แหล่งข้อมูลประกาศ
  bool goldEstimate = false;     // true = ประมาณจากราคาโลก
  // ดัชนี SET
  bool setValid = false;
  float setPrice = NAN, setPrev = NAN, setChange = NAN, setPct = NAN;
  String setState;               // open / break / closed
  float spark[60];
  int sparkCount = 0;
  float set50Price = NAN, set50Change = NAN, set50Pct = NAN;
  // อื่น ๆ
  float xau = NAN, xauPct = NAN, usdthb = NAN, usdthbChange = NAN;
  String error;
};

namespace MarketApi {
bool fetch(Market& out);
}
