// เสียงผ่านชิป ES8311 (I2C 0x18 บนบัสเดียวกับทัช) + I2S
// ขา I2S ของบอร์ดแต่ละล็อตอาจต่างกัน จึงทดสอบครั้งแรกโดยให้ผู้ใช้แตะจอเมื่อได้ยินเสียง
#include "sound.h"
#include <ESP_I2S.h>
#include <Preferences.h>
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#include "display.h"

namespace {

constexpr int I2C_PORT = 1, PIN_SDA = 16, PIN_SCL = 15;
constexpr uint8_t ES_ADDR = 0x18;
constexpr int RATE = 16000;
constexpr int PIN_MCLK = 4, PIN_BCLK = 5, PIN_WS = 7;

struct Cfg { int8_t dout, pa; uint8_t paOn; };
const Cfg CFGS[] = {{6, 1, LOW}, {6, 1, HIGH}, {8, 1, LOW}, {8, 1, HIGH}};
constexpr int CFG_COUNT = sizeof(CFGS) / sizeof(CFGS[0]);

I2SClass i2s;
bool codec = false, i2sOn = false;
int cfgIdx = -1;     // ชุดขาที่ใช้ได้ (-1 = ยังไม่รู้)
uint8_t tries = 0;
QueueHandle_t queue = nullptr;
Preferences prefs;

bool wr(uint8_t reg, uint8_t v) { return lgfx::i2c::writeRegister8(I2C_PORT, ES_ADDR, reg, v, 0, 100000).has_value(); }
int rd(uint8_t reg) {
  auto r = lgfx::i2c::readRegister8(I2C_PORT, ES_ADDR, reg, 100000);
  return r.has_value() ? r.value() : -1;
}

// ค่าเริ่มต้น ES8311: slave, MCLK = 256 x 16 kHz, I2S 16 บิต, เปิดเฉพาะ DAC
void codecInit() {
  wr(0x00, 0x1F); delay(20);
  wr(0x00, 0x00);
  wr(0x45, 0x00);
  wr(0x01, 0x30);
  wr(0x02, 0x00);
  wr(0x03, 0x10);
  wr(0x16, 0x24);
  wr(0x04, 0x10);
  wr(0x05, 0x00);
  wr(0x0B, 0x00);
  wr(0x0C, 0x00);
  wr(0x10, 0x1F);
  wr(0x11, 0x7F);
  wr(0x00, 0x80);   // เปิดเครื่อง โหมด slave
  wr(0x01, 0x3F);   // ใช้ MCLK จากขา
  wr(0x06, 0x03);   // BCLK = MCLK / 4
  wr(0x07, 0x00);
  wr(0x08, 0xFF);   // LRCK = MCLK / 256
  wr(0x09, 0x0C);   // DAC รับ I2S 16 บิต
  wr(0x0A, 0x0C);
  wr(0x13, 0x10);
  wr(0x1B, 0x0A);
  wr(0x1C, 0x6A);
  wr(0x17, 0xBF);
  wr(0x0E, 0x02);
  wr(0x12, 0x00);
  wr(0x14, 0x1A);
  wr(0x0D, 0x01);
  wr(0x15, 0x40);
  wr(0x37, 0x08);
  wr(0x45, 0x00);
  wr(0x32, SOUND_VOLUME);  // ระดับเสียง DAC (0xBF = 0 dB)
  wr(0x31, 0x00);          // ไม่ปิดเสียง
}

void paSet(bool on) {
  if (cfgIdx < 0) return;
  const Cfg& c = CFGS[cfgIdx];
  digitalWrite(c.pa, on ? c.paOn : !c.paOn);
}

bool useCfg(int idx) {
  if (i2sOn) { i2s.end(); i2sOn = false; }
  if (cfgIdx >= 0) { pinMode(CFGS[cfgIdx].pa, INPUT); }
  cfgIdx = idx;
  if (idx < 0) return false;
  const Cfg& c = CFGS[idx];
  pinMode(c.pa, OUTPUT);
  paSet(false);
  i2s.setPins(PIN_BCLK, PIN_WS, c.dout, -1, PIN_MCLK);
  i2sOn = i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (i2sOn) codecInit();  // ชิปต้องมี MCLK ก่อนตั้งค่า
  return i2sOn;
}

// ----- สังเคราะห์เสียงระฆัง (เมโลดี้แต่งเอง) -----
struct Note { uint16_t hz, at, dur; };  // ms
struct Mel { const Note* n; uint8_t count; uint16_t total; };
const Note N_TEST[] = {{1047, 0, 500}, {1568, 180, 700}};
const Note N_BOOT[] = {{523, 0, 500}, {659, 110, 500}, {784, 220, 500}, {1047, 330, 1100}};
const Note N_RAIN[] = {{880, 0, 260}, {659, 170, 260}, {880, 340, 260}, {659, 510, 260},
                       {880, 900, 260}, {659, 1070, 260}, {440, 1240, 900}};
const Note N_HOUR[] = {{784, 0, 1200}, {659, 420, 1200}, {523, 840, 1600}};
const Note N_OK[] = {{784, 0, 300}, {1047, 120, 700}};
const Mel MELS[] = {{N_TEST, 2, 900}, {N_BOOT, 4, 1450}, {N_RAIN, 7, 2150}, {N_HOUR, 3, 2450}, {N_OK, 2, 850}};

void render(const Mel& m) {
  static int16_t buf[256 * 2];
  const int totalSamples = (int)m.total * RATE / 1000;
  const float amp = 9000.0f;
  for (int s0 = 0; s0 < totalSamples; s0 += 256) {
    int n = min(256, totalSamples - s0);
    for (int k = 0; k < n; k++) {
      int s = s0 + k;
      float v = 0;
      for (int i = 0; i < m.count; i++) {
        const Note& no = m.n[i];
        int st = no.at * RATE / 1000, len = no.dur * RATE / 1000;
        int t = s - st;
        if (t < 0 || t >= len) continue;
        float tt = (float)t / RATE;
        float env = expf(-tt * 5.0f) * min(1.0f, t / 48.0f) * min(1.0f, (len - t) / 400.0f);
        float ph = 6.2831853f * no.hz * tt;
        v += env * (sinf(ph) + 0.35f * sinf(2 * ph) + 0.12f * sinf(3.01f * ph));
      }
      int16_t q = (int16_t)constrain(v * amp, -32000.0f, 32000.0f);
      buf[k * 2] = q;
      buf[k * 2 + 1] = q;
    }
    i2s.write((uint8_t*)buf, n * 4);
  }
  // เติมความเงียบสั้น ๆ ให้ DMA ปล่อยเสียงสุดท้ายออกจนหมด
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 8; i++) i2s.write((uint8_t*)buf, sizeof(buf));
}

void task(void*) {
  uint8_t m;
  for (;;) {
    if (xQueueReceive(queue, &m, portMAX_DELAY) != pdTRUE) continue;
    if (!i2sOn || m >= sizeof(MELS) / sizeof(MELS[0])) continue;
    paSet(true);
    delay(30);
    render(MELS[m]);
    paSet(false);
  }
}

void save() {
  prefs.begin("s3snd", false);
  prefs.putChar("cfg", cfgIdx);
  prefs.putUChar("tries", tries);
  prefs.end();
}

}  // namespace

namespace Sound {

bool begin() {
  if (rd(0xFD) < 0) lgfx::i2c::init(I2C_PORT, PIN_SDA, PIN_SCL);  // ไม่มีทัช → เปิดบัสเอง
  int id1 = rd(0xFD), id2 = rd(0xFE);
  codec = id1 == 0x83;
  Serial.printf("[Sound] ชิปเสียง ES8311: %s (id %02X %02X)\n", codec ? "พบ" : "ไม่พบ", id1 & 0xFF, id2 & 0xFF);
  if (!codec) return false;
  prefs.begin("s3snd", true);
  int saved = prefs.getChar("cfg", -1);
  tries = prefs.getUChar("tries", 0);
  prefs.end();
  queue = xQueueCreate(4, 1);
  xTaskCreatePinnedToCore(task, "sound", 6144, nullptr, 2, nullptr, 0);
  if (saved >= 0 && saved < CFG_COUNT) useCfg(saved);
  return true;
}

bool needsTest() { return codec && cfgIdx < 0 && tries < 3 && Display::hasTouch(); }
bool ready() { return codec && cfgIdx >= 0 && i2sOn; }

void play(Melody m) {
  if (!ready() || !queue) return;
  uint8_t v = m;
  xQueueSend(queue, &v, 0);
}

void runTest() {
  if (!codec) return;
  tries++;
  save();
  for (int i = 0; i < CFG_COUNT; i++) {
    if (!useCfg(i)) continue;
    Display::showSoundTest(i + 1, CFG_COUNT);
    bool heard = false;
    for (int rep = 0; rep < 3 && !heard; rep++) {
      uint8_t v = MEL_TEST;
      xQueueSend(queue, &v, 0);
      heard = Display::waitTap(2500);
    }
    if (heard) {
      Serial.printf("[Sound] ใช้ชุดขาที่ %d (DOUT %d, PA %d=%s)\n", i + 1, CFGS[i].dout, CFGS[i].pa,
                    CFGS[i].paOn ? "HIGH" : "LOW");
      save();
      Display::showSoundOk();
      play(MEL_OK);
      delay(1500);
      return;
    }
  }
  useCfg(-1);
  save();
  Serial.println("[Sound] ไม่มีใครแตะจอ — ปิดเสียงไว้ก่อน (พิมพ์ sound เพื่อทดสอบใหม่)");
}

void loop() {
  static int lastHour = -1;
  struct tm t;
  if (!ready() || !getLocalTime(&t, 0)) return;
  if (t.tm_min == 0 && t.tm_hour != lastHour) {
    lastHour = t.tm_hour;
    if (t.tm_hour >= CHIME_FROM_HOUR && t.tm_hour <= CHIME_TO_HOUR) play(MEL_HOUR);
  }
}

String status() {
  if (!codec) return "ไม่พบชิปเสียง";
  if (cfgIdx < 0) return "ยังไม่ได้ทดสอบลำโพง";
  return String("พร้อม (ชุดที่ ") + (cfgIdx + 1) + ")";
}

}  // namespace Sound
