// ทดสอบจอสำหรับบอร์ดที่จอขาว — ลองต่อจอหลายแบบ แล้วรายงานผลทาง Serial (115200)
// แต่ละแบบ: อ่านรหัสชิปจอ + วาดตัวเลขใหญ่ (เลขแบบ) และแถบสี แดง/เขียว/น้ำเงิน ค้างไว้ 6 วินาที
#include <SPI.h>
#include <esp_chip_info.h>
#include <esp_mac.h>

struct Wiring { const char* name; int sck, mosi, miso, cs, dc, rst; };
const Wiring WIRINGS[] = {
  {"CYD standard (HSPI 14/13/12, CS15, DC2)", 14, 13, 12, 15, 2, -1},
  {"CYD + reset GPIO4",                      14, 13, 12, 15, 2, 4},
  {"VSPI 18/23/19, CS15, DC2, RST4",         18, 23, 19, 15, 2, 4},
  {"VSPI 18/23/19, CS5, DC2",                18, 23, 19, 5, 2, -1},
  {"VSPI 18/23/19, CS5, DC16, RST17",        18, 23, 19, 5, 16, 17},
  {"HSPI 14/13/12, CS15, DC27",              14, 13, 12, 15, 27, -1},
};
const int N = sizeof(WIRINGS) / sizeof(WIRINGS[0]);
const int BACKLIGHTS[] = {21, 27};

SPIClass* spi = nullptr;
const Wiring* W = nullptr;

void cmd(uint8_t c) { digitalWrite(W->dc, LOW); digitalWrite(W->cs, LOW); spi->transfer(c); digitalWrite(W->cs, HIGH); }
void dat(uint8_t d) { digitalWrite(W->dc, HIGH); digitalWrite(W->cs, LOW); spi->transfer(d); digitalWrite(W->cs, HIGH); }
void cd(uint8_t c, std::initializer_list<uint8_t> ds) { cmd(c); for (uint8_t d : ds) dat(d); }

String readId(uint8_t c) {
  spi->beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(W->cs, LOW);
  digitalWrite(W->dc, LOW);
  spi->transfer(c);
  digitalWrite(W->dc, HIGH);
  spi->transfer(0);  // dummy
  char buf[16];
  uint8_t a = spi->transfer(0), b = spi->transfer(0), d = spi->transfer(0);
  digitalWrite(W->cs, HIGH);
  spi->endTransaction();
  snprintf(buf, sizeof(buf), "%02X %02X %02X", a, b, d);
  return buf;
}

void fill(int x, int y, int w, int h, uint16_t col) {
  cd(0x2A, {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1)});
  cd(0x2B, {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1)});
  cmd(0x2C);
  digitalWrite(W->dc, HIGH);
  digitalWrite(W->cs, LOW);
  for (int i = 0; i < w * h; i++) spi->transfer16(col);
  digitalWrite(W->cs, HIGH);
}

void digit(int n) {
  const uint8_t seg[10] = {0b1111110, 0b0110000, 0b1101101, 0b1111001, 0b0110011,
                           0b1011011, 0b1011111, 0b1110000, 0b1111111, 0b1111011};
  const int x = 60, y = 50, w = 120, h = 200, t = 22;
  uint8_t s = seg[n % 10];
  if (s & 0x40) fill(x, y, w, t, 0xFFFF);
  if (s & 0x20) fill(x + w - t, y, t, h / 2, 0xFFFF);
  if (s & 0x10) fill(x + w - t, y + h / 2, t, h / 2, 0xFFFF);
  if (s & 0x08) fill(x, y + h - t, w, t, 0xFFFF);
  if (s & 0x04) fill(x, y + h / 2, t, h / 2, 0xFFFF);
  if (s & 0x02) fill(x, y, t, h / 2, 0xFFFF);
  if (s & 0x01) fill(x, y + h / 2 - t / 2, w, t, 0xFFFF);
}

bool looksValid(const String& id) { return id != "00 00 00" && id != "FF FF FF"; }

void tryWiring(int idx) {
  W = &WIRINGS[idx];
  if (spi) { spi->end(); delete spi; }
  spi = new SPIClass((W->sck == 14) ? HSPI : VSPI);
  pinMode(W->cs, OUTPUT);
  digitalWrite(W->cs, HIGH);
  pinMode(W->dc, OUTPUT);
  spi->begin(W->sck, W->miso, W->mosi, -1);
  if (W->rst >= 0) {
    pinMode(W->rst, OUTPUT);
    digitalWrite(W->rst, HIGH); delay(5);
    digitalWrite(W->rst, LOW); delay(20);
    digitalWrite(W->rst, HIGH); delay(150);
  }
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cmd(0x01);
  spi->endTransaction();
  delay(150);
  String id04 = readId(0x04), idD3 = readId(0xD3), id09 = readId(0x09);
  bool ok = looksValid(id04) || looksValid(idD3);
  Serial.printf("\n=== แบบที่ %d: %s ===\n", idx + 1, W->name);
  Serial.printf("  รหัสชิป 0x04: %s | 0xD3: %s | 0x09: %s  -> %s\n", id04.c_str(), idD3.c_str(), id09.c_str(),
                ok ? "✅ จอตอบกลับ!" : "❌ ไม่ตอบ");
  if (id04.startsWith("85 85")) Serial.println("  ชิปจอ: ST7789");
  if (idD3.indexOf("93 41") >= 0) Serial.println("  ชิปจอ: ILI9341");
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cd(0xCF, {0x00, 0xC1, 0x30});  // ILI9341 power (ST7789 ไม่สนใจ)
  cd(0xC0, {0x23});
  cd(0xC1, {0x10});
  cd(0xC5, {0x3E, 0x28});
  cmd(0x11);
  spi->endTransaction();
  delay(120);
  spi->beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0));
  cd(0x3A, {0x55});
  cd(0x36, {0x08});
  cmd(0x20);
  cmd(0x13);
  cmd(0x29);
  fill(0, 0, 240, 320, 0x0000);
  digit(idx + 1);
  fill(0, 280, 80, 40, 0xF800);
  fill(80, 280, 80, 40, 0x07E0);
  fill(160, 280, 80, 40, 0x001F);
  spi->endTransaction();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  for (int p : BACKLIGHTS) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.println("\n\n########## ทดสอบจอ ESP32 (CYD display scan) ##########");
  Serial.printf("ชิป ESP32 rev %d, %d core, Flash %u MB, PSRAM %s, MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                ci.revision, ci.cores, ESP.getFlashChipSize() / (1024 * 1024), psramFound() ? "มี" : "ไม่มี",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("จะลองต่อจอ 6 แบบ แบบละ 6 วินาที — ดูที่จอว่าแบบไหนมี 'ตัวเลขใหญ่' ขึ้น");
}

void loop() {
  for (int i = 0; i < N; i++) {
    tryWiring(i);
    delay(6000);
  }
  Serial.println("\n----- วนรอบใหม่ -----");
}
