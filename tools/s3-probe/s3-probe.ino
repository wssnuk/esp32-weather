// ตรวจบอร์ด ESP32-S3 จอ 2.8" (ทัช capacitive) — หาวิธีต่อจอ/ทัช/เสียง แล้วรายงานทาง Serial 115200
// แต่ละแบบของจอจะแสดง "ตัวเลขใหญ่" + แถบสี แดง/เขียว/น้ำเงิน + แถบไล่สีเทา (ทดสอบสีกลาง ๆ)
#include <SPI.h>
#include <Wire.h>
#include <esp_chip_info.h>
#include <esp_log.h>

// พิมพ์ทั้ง USB (CDC) และ UART0 — บอร์ดบางรุ่นใช้ชิป USB-Serial แยก
void say(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  Serial0.print(buf);
}

struct Wiring { const char* name; int sck, mosi, miso, cs, dc, rst, bl; };
const Wiring WIRINGS[] = {
  {"S3 2.8 (Hosyond/LCDwiki/Freenove): SCK12 MOSI11 MISO13 CS10 DC46 BL45", 12, 11, 13, 10, 46, -1, 45},
  {"Waveshare S3-Touch-LCD-2.8: SCK40 MOSI45 MISO46 CS42 DC41 RST39 BL5", 40, 45, 46, 42, 41, 39, 5},
  {"SCK12 MOSI11 MISO13 CS10 DC9 RST14 BL21", 12, 11, 13, 10, 9, 14, 21},
  {"SCK14 MOSI13 MISO12 CS15 DC2 BL21 (CYD style)", 14, 13, 12, 15, 2, -1, 21},
  {"SCK7 MOSI6 CS5 DC4 RST48 BL38", 7, 6, -1, 5, 4, 48, 38},
};
const int N = sizeof(WIRINGS) / sizeof(WIRINGS[0]);
SPIClass* spi = nullptr;
const Wiring* W = nullptr;

void cmd(uint8_t c) { digitalWrite(W->dc, LOW); digitalWrite(W->cs, LOW); spi->transfer(c); digitalWrite(W->cs, HIGH); }
void dat(uint8_t d) { digitalWrite(W->dc, HIGH); digitalWrite(W->cs, LOW); spi->transfer(d); digitalWrite(W->cs, HIGH); }
void cd(uint8_t c, std::initializer_list<uint8_t> ds) { cmd(c); for (uint8_t d : ds) dat(d); }
String readId(uint8_t c) {
  if (W->miso < 0) return "-- -- --";
  spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(W->cs, LOW); digitalWrite(W->dc, LOW); spi->transfer(c); digitalWrite(W->dc, HIGH);
  spi->transfer(0);
  uint8_t a = spi->transfer(0), b = spi->transfer(0), d = spi->transfer(0);
  digitalWrite(W->cs, HIGH);
  spi->endTransaction();
  char buf[16]; snprintf(buf, sizeof(buf), "%02X %02X %02X", a, b, d); return buf;
}
void win(int x, int y, int w, int h) {
  cd(0x2A, {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1)});
  cd(0x2B, {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1)});
  cmd(0x2C); digitalWrite(W->dc, HIGH); digitalWrite(W->cs, LOW);
}
void fill(int x, int y, int w, int h, uint16_t col) { win(x, y, w, h); for (int i = 0; i < w * h; i++) spi->transfer16(col); digitalWrite(W->cs, HIGH); }
void ramp(int y, int h, int ch) {  // ch: 0 เทา, 1 แดง, 2 เขียว, 3 น้ำเงิน
  win(0, y, 240, h);
  for (int r = 0; r < h; r++) for (int x = 0; x < 240; x++) {
    uint8_t v = x * 255 / 239;
    uint16_t col = ch == 0 ? ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3) : ch == 1 ? (v >> 3) << 11 : ch == 2 ? (v >> 2) << 5 : (v >> 3);
    spi->transfer16(col);
  }
  digitalWrite(W->cs, HIGH);
}
void digit(int n) {
  const uint8_t seg[10] = {0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B};
  const int x = 70, y = 20, w = 100, h = 150, t = 18; uint8_t s = seg[n % 10];
  if (s & 0x40) fill(x, y, w, t, 0xFFFF);
  if (s & 0x20) fill(x + w - t, y, t, h / 2, 0xFFFF);
  if (s & 0x10) fill(x + w - t, y + h / 2, t, h / 2, 0xFFFF);
  if (s & 0x08) fill(x, y + h - t, w, t, 0xFFFF);
  if (s & 0x04) fill(x, y + h / 2, t, h / 2, 0xFFFF);
  if (s & 0x02) fill(x, y, t, h / 2, 0xFFFF);
  if (s & 0x01) fill(x, y + h / 2 - t / 2, w, t, 0xFFFF);
}
bool valid(const String& id) { return id != "00 00 00" && id != "FF FF FF" && id != "-- -- --"; }

void tryWiring(int i) {
  W = &WIRINGS[i];
  if (spi) { spi->end(); delete spi; }
  spi = new SPIClass(FSPI);
  pinMode(W->cs, OUTPUT); digitalWrite(W->cs, HIGH); pinMode(W->dc, OUTPUT);
  if (W->bl >= 0) { pinMode(W->bl, OUTPUT); digitalWrite(W->bl, HIGH); }
  spi->begin(W->sck, W->miso, W->mosi, -1);
  if (W->rst >= 0) { pinMode(W->rst, OUTPUT); digitalWrite(W->rst, LOW); delay(20); digitalWrite(W->rst, HIGH); delay(150); }
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); cmd(0x01); spi->endTransaction(); delay(150);
  String id04 = readId(0x04), idD3 = readId(0xD3);
  say("\n=== จอแบบที่ %d: %s ===\n  รหัสชิป 0x04: %s | 0xD3: %s -> %s\n", i + 1, W->name, id04.c_str(), idD3.c_str(),
      valid(id04) || valid(idD3) ? "✅ จอตอบกลับ" : "❌ ไม่ตอบ");
  displaySummary += "  แบบ " + String(i + 1) + ": 0x04=" + id04 + " 0xD3=" + idD3 + "\n";
  if (id04.startsWith("85 85")) say("  ชิปจอ: ST7789\n");
  if (idD3.indexOf("93 41") >= 0) say("  ชิปจอ: ILI9341\n");
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cd(0xCF, {0x00, 0xC1, 0x30}); cd(0xC0, {0x23}); cd(0xC1, {0x10}); cd(0xC5, {0x3E, 0x28});
  cmd(0x11); spi->endTransaction(); delay(120);
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cd(0x3A, {0x55}); cd(0x36, {0x08}); cmd(0x20); cmd(0x13); cmd(0x29);
  fill(0, 0, 240, 320, 0x0000);
  digit(i + 1);
  fill(0, 190, 80, 30, 0xF800); fill(80, 190, 80, 30, 0x07E0); fill(160, 190, 80, 30, 0x001F);
  ramp(228, 22, 0); ramp(252, 20, 1); ramp(274, 20, 2); ramp(296, 20, 3);
  spi->endTransaction();
}

const char* i2cName(uint8_t a) {
  switch (a) {
    case 0x38: return "ทัช FT6336/FT5x06";
    case 0x15: return "ทัช CST816";
    case 0x1A: return "ทัช CST328";
    case 0x5D: case 0x14: return "ทัช GT911";
    case 0x18: return "ชิปเสียง ES8311";
    case 0x40: return "ชิปไมค์ ES7210/อื่น ๆ";
    case 0x51: return "นาฬิกา RTC PCF85063/8563";
    case 0x6B: case 0x6A: return "เซนเซอร์ QMI8658/IMU";
    case 0x34: return "ชิปไฟ AXP";
    case 0x3C: return "จอ OLED / กล้อง?";
    case 0x30: return "กล้อง OV?";
    default: return "?";
  }
}

String i2cSummary;
String chipSummary;
String displaySummary;

void scanI2C() {
  i2cSummary = "";
  const int pairs[][2] = {{16, 15}, {15, 16}, {1, 3}, {3, 1}, {8, 9}, {9, 8}, {4, 5}, {5, 4}, {6, 7}, {7, 6},
                          {17, 18}, {18, 17}, {39, 40}, {40, 39}, {47, 48}, {48, 47}, {2, 1}, {1, 2}, {38, 39}};
  say("\n----- ค้นหาอุปกรณ์ I2C (ทัช / เสียง / RTC) -----\n");
  for (auto& p : pairs) {
    Wire.begin(p[0], p[1], 100000);
    String found;
    for (uint8_t a = 0x08; a < 0x78; a++) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) { found += String(" 0x") + String(a, HEX) + "(" + i2cName(a) + ")"; }
    }
    Wire.end();
    if (found.length()) {
      say("  SDA %d / SCL %d :%s\n", p[0], p[1], found.c_str());
      i2cSummary += "  SDA " + String(p[0]) + " / SCL " + String(p[1]) + " :" + found + "\n";
    }
    delay(5);
  }
  say("----- ค้นหา I2C เสร็จ -----\n");
}

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);  // ปิด log ภายใน (เช่น error ตอนสแกน I2C) ให้อ่านผลง่าย
  Serial.begin(115200);
  Serial0.begin(115200);
  delay(1500);
  esp_chip_info_t ci; esp_chip_info(&ci);
  say("\n\n########## ตรวจบอร์ด ESP32-S3 (s3-probe) ##########\n");
  char cs[160];
  snprintf(cs, sizeof(cs), "ชิป %s rev %d, %d core | Flash %u MB | PSRAM %u KB", ESP.getChipModel(), ci.revision,
           ci.cores, ESP.getFlashChipSize() / (1024 * 1024), ESP.getPsramSize() / 1024);
  chipSummary = cs;
  say("%s\n", cs);
  scanI2C();
  say("\nจะลองต่อจอ %d แบบ แบบละ 7 วินาที — ดูที่จอว่าแบบไหนมีตัวเลขใหญ่ขึ้น\n", N);
}

void loop() {
  displaySummary = "";
  for (int i = 0; i < N; i++) { tryWiring(i); delay(7000); }
  say("\n\n================ สรุปผล (ถ่ายรูปส่วนนี้) ================\n");
  say("%s\n", chipSummary.c_str());
  say("I2C ที่พบ:\n%s", i2cSummary.length() ? i2cSummary.c_str() : "  (ไม่พบ)\n");
  say("จอ:\n%s", displaySummary.c_str());
  say("=========================================================\n");
  say("จะวนทดสอบจอใหม่อีกรอบ...\n");
}
