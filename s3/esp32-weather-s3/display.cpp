// ธีม Aurora Gold สำหรับบอร์ด ESP32-S3 2.8" (240x320, ILI9341 SPI, ทัช capacitive)
// วาดทุกอย่างลง sprite ใน PSRAM แล้วส่งขึ้นจอทีเดียว (ไม่กะพริบ)
#include "display.h"
#include <WiFi.h>
#include <Wire.h>
#include <esp_log.h>
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "assets.h"
#include "radar.h"
#include "lunar.h"
#include "net.h"
#include "config.h"
#include <Preferences.h>

// ---------------- ขาของบอร์ด (ยืนยันจากโปรแกรมตรวจบอร์ด: จอแบบที่ 1 + กลับสี) ----------------
#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_CS 10
#define PIN_DC 46
#define PIN_BL 45

namespace {

// "โหมดภาพ" = ชุดคำสั่งเริ่มจอ (แรงดัน/แกมมา) จอแต่ละล็อตต้องใช้ชุดต่างกัน ไม่งั้นภาพจะซีด/มืด
uint8_t panelPreset = 0;
constexpr uint8_t PRESET_COUNT = 4;
constexpr uint8_t CMD_DELAY = 0x80;
// 0: ชุดสั้นแบบเดิม (เวอร์ชัน 1.0-1.2)
const uint8_t P_OLD[] = {
    0x01, 0 + CMD_DELAY, 150,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xC0, 1, 0x23,
    0xC1, 1, 0x10,
    0xC5, 2, 0x3E, 0x28,
    0x11, 0 + CMD_DELAY, 120,
    0x13, 0,
    0x29, 0,
    0xFF, 0xFF,
};
// 1: ILI9341V จอ IPS (ชุดของผู้ผลิตจอ — แรงดัน+แกมมาสำหรับจอ IPS)
const uint8_t P_IPS[] = {
    0x01, 0 + CMD_DELAY, 150,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x13,
    0xC1, 1, 0x13,
    0xC5, 2, 0x22, 0x35,
    0xC7, 1, 0xBD,
    0xB6, 2, 0x0A, 0x82,   // ทิศการสแกนต้องตรงกับโหมดเดิม (0xA2 = กลับซ้าย-ขวา)
    0xF6, 2, 0x01, 0x30,
    0xB1, 2, 0x00, 0x1B,
    0xF2, 1, 0x00,
    0x26, 1, 0x01,
    0xE0, 15, 0x0F, 0x35, 0x31, 0x0B, 0x0E, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00,
    0xE1, 15, 0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F,
    0x11, 0 + CMD_DELAY, 120,
    0x29, 0 + CMD_DELAY, 50,
    0xFF, 0xFF,
};
// 2: ILI9341 มาตรฐาน (มีแกมมาเต็มชุด)
const uint8_t P_ILI[] = {
    0x01, 0 + CMD_DELAY, 150,
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x23,
    0xC1, 1, 0x10,
    0xC5, 2, 0x3E, 0x28,
    0xC7, 1, 0x86,
    0xB1, 2, 0x00, 0x13,
    0xF2, 1, 0x00,
    0x26, 1, 0x01,
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0xB6, 3, 0x08, 0x82, 0x27,   // (0xC2 = กลับหัว)
    0x11, 0 + CMD_DELAY, 120,
    0x29, 0 + CMD_DELAY, 50,
    0xFF, 0xFF,
};
// 3: ST7789 (บางล็อตใช้ชิปนี้แทน)
const uint8_t P_ST[] = {
    0x01, 0 + CMD_DELAY, 150,
    0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
    0xB7, 1, 0x35,
    0xBB, 1, 0x28,
    0xC0, 1, 0x0C,
    0xC2, 2, 0x01, 0xFF,
    0xC3, 1, 0x10,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA4, 0xA1,
    0xE0, 14, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
    0xE1, 14, 0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
    0x11, 0 + CMD_DELAY, 130,
    0x13, 0,
    0x29, 0 + CMD_DELAY, 50,
    0xFF, 0xFF,
};
const uint8_t* const PRESETS[PRESET_COUNT] = {P_OLD, P_IPS, P_ILI, P_ST};

class PanelS3 : public lgfx::Panel_ILI9341 {
 protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    return listno == 0 ? PRESETS[panelPreset % PRESET_COUNT] : nullptr;
  }
};

enum TouchKind { TOUCH_NONE, TOUCH_FT, TOUCH_CST, TOUCH_GT };

class LGFX : public lgfx::LGFX_Device {
  PanelS3 panel;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
  lgfx::Touch_FT5x06 ft;
  lgfx::Touch_CST816S cst;
  lgfx::Touch_GT911 gt;

 public:
  LGFX(TouchKind tk, int sda, int scl, uint8_t addr) {
    {
      auto c = bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.pin_sclk = PIN_SCK;
      c.pin_mosi = PIN_MOSI;
      c.pin_miso = PIN_MISO;
      c.pin_dc = PIN_DC;
      bus.config(c);
      panel.setBus(&bus);
    }
    {
      auto c = panel.config();
      c.pin_cs = PIN_CS;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 320;
      c.readable = false;
      c.invert = true;
      c.rgb_order = false;
      c.bus_shared = false;
      panel.config(c);
    }
    {
      auto c = light.config();
      c.pin_bl = PIN_BL;
      c.freq = 12000;
      c.pwm_channel = 7;
      light.config(c);
      panel.setLight(&light);
    }
    lgfx::ITouch* t = nullptr;
    if (tk == TOUCH_FT) t = &ft;
    else if (tk == TOUCH_CST) t = &cst;
    else if (tk == TOUCH_GT) t = &gt;
    if (t) {
      auto c = t->config();
      c.i2c_port = 1;
      c.pin_sda = sda;
      c.pin_scl = scl;
      c.i2c_addr = addr;
      c.freq = 400000;
      c.pin_int = -1;
      c.pin_rst = -1;
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 319;
      c.bus_shared = false;
      t->config(c);
      panel.setTouch(t);
    }
    setPanel(&panel);
  }
};

LGFX* gfx = nullptr;
LGFX_Sprite* canvas = nullptr;  // เฟรมที่กำลังวาด
LGFX_Sprite* bgCache = nullptr; // พื้นหลังของหน้าปัจจุบัน
const ImgAsset* bgLoaded = nullptr;
lgfx::LovyanGFX* g = nullptr;   // เป้าหมายการวาด (canvas หรือจอโดยตรงถ้าไม่มี PSRAM)

// ---------------- ค้นหาทัชอัตโนมัติ ----------------
TouchKind touchKind = TOUCH_NONE;
int touchSda = -1, touchScl = -1;
uint8_t touchAddr = 0;

void detectTouch() {
  static const int pairs[][2] = {{16, 15}, {15, 16}, {8, 9}, {9, 8}, {1, 3}, {3, 1}, {4, 5}, {5, 4},
                                 {6, 7},   {7, 6},   {17, 18}, {18, 17}, {39, 40}, {40, 39}, {47, 48}, {48, 47}};
  esp_log_level_set("*", ESP_LOG_NONE);
  for (auto& p : pairs) {
    Wire.begin(p[0], p[1], 100000);
    for (uint8_t a : {0x38, 0x15, 0x5D, 0x14}) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        touchSda = p[0];
        touchScl = p[1];
        touchAddr = a;
        touchKind = a == 0x38 ? TOUCH_FT : a == 0x15 ? TOUCH_CST : TOUCH_GT;
        break;
      }
    }
    Wire.end();
    if (touchKind != TOUCH_NONE) break;
  }
  esp_log_level_set("*", ESP_LOG_WARN);
  const char* names[] = {"ไม่พบ", "FT6336/FT5x06", "CST816", "GT911"};
  Serial.printf("[Touch] %s", names[touchKind]);
  if (touchKind != TOUCH_NONE) Serial.printf(" ที่ SDA %d / SCL %d (0x%02X)", touchSda, touchScl, touchAddr);
  Serial.println();
}

// ---------------- สีและตัวช่วย ----------------
inline uint32_t RGB(uint8_t r, uint8_t g_, uint8_t b) { return ((uint32_t)r << 16) | ((uint32_t)g_ << 8) | b; }
const uint32_t C_WHITE = RGB(255, 255, 255), C_GOLD = RGB(255, 214, 120), C_MUTED = RGB(200, 214, 240),
               C_TEAL = RGB(0, 230, 255), C_RAIN = RGB(90, 170, 255), C_FEEL = RGB(255, 150, 90),
               C_UV = RGB(255, 100, 220), C_RED = RGB(255, 90, 110), C_VIOLET = RGB(170, 100, 255),
               C_GREEN = RGB(120, 240, 160);

uint32_t pmColor(float v) {
  int lv = WeatherApi::pm25Level(v);
  static const uint32_t c[] = {RGB(90, 210, 255), RGB(130, 240, 160), RGB(250, 220, 90), RGB(255, 140, 90), RGB(230, 90, 200)};
  return lv >= 0 ? c[lv] : C_WHITE;
}
uint32_t uvColor(float v) {
  int lv = WeatherApi::uvLevel(v);
  static const uint32_t c[] = {RGB(130, 240, 160), RGB(250, 220, 90), RGB(255, 160, 90), RGB(255, 110, 120), RGB(220, 120, 255)};
  return lv >= 0 ? c[lv] : C_WHITE;
}

void useFont(int px) {
  static int loaded = -1;
  if (loaded == px) return;
  const uint8_t* f = px == 9 ? VLW_9 : px == 11 ? VLW_11 : px == 14 ? VLW_14 : px == 18 ? VLW_18 : px == 30 ? VLW_30 : VLW_24;
  g->unloadFont();
  g->loadFont(f);
  loaded = px;
}
void txt(const String& s, int x, int y, int px, uint32_t col, textdatum_t datum = top_left) {
  useFont(px);
  g->setTextColor(col);
  g->setTextDatum(datum);
  g->drawString(s, x, y);
}
int txtW(const String& s, int px) {
  useFont(px);
  return g->textWidth(s);
}

// ข้อความไทย (alpha 4 บิต) ผสมกับพื้นหลังจริง → ขอบเรียบบนพื้นไล่สี
int thaiW(ThaiId id) { return THAI[id].w; }
int thaiH(ThaiId id) { return THAI[id].h; }
void thai(ThaiId id, int x, int y, uint32_t col, int align = 0) {  // align: 0 ซ้าย, 1 กลาง, 2 ขวา ; y = กึ่งกลางแนวตั้ง
  const ThaiAsset& t = THAI[id];
  if (align == 1) x -= t.w / 2;
  else if (align == 2) x -= t.w;
  y -= t.h / 2;
  uint8_t fr = col >> 16, fg = (col >> 8) & 0xFF, fb = col & 0xFF;
  int rowBytes = (t.w + 1) / 2;
  for (int yy = 0; yy < t.h; yy++) {
    int py = y + yy;
    if (py < 0 || py >= 240) continue;
    const uint8_t* row = t.data + yy * rowBytes;
    for (int xx = 0; xx < t.w; xx++) {
      uint8_t v = pgm_read_byte(row + xx / 2);
      uint8_t a = (xx & 1) ? (v & 0x0F) : (v >> 4);
      if (!a) continue;
      int px = x + xx;
      if (px < 0 || px >= 320) continue;
      if (a == 15) {
        g->drawPixel(px, py, g->color888(fr, fg, fb));
      } else {
        auto b = g->readPixelRGB(px, py);
        g->drawPixel(px, py, g->color888(b.R8() + (fr - b.R8()) * a / 15, b.G8() + (fg - b.G8()) * a / 15,
                                         b.B8() + (fb - b.B8()) * a / 15));
      }
    }
  }
}

// ตัวเลขทอง/วินาที (PNG โปร่งใส)
int glyphIndex(char c) { return c >= '0' && c <= '9' ? c - '0' : c == ':' ? 10 : 11; }
int drawGlyphs(const GlyphAsset* set, const char* s, int x, int top) {
  for (const char* p = s; *p; p++) {
    const GlyphAsset& a = set[glyphIndex(*p)];
    g->drawPng(a.data, a.len, x - a.pad, top - a.pad);
    x += a.adv;
  }
  return x;
}
int glyphsWidth(const GlyphAsset* set, const char* s) {
  int w = 0;
  for (const char* p = s; *p; p++) w += set[glyphIndex(*p)].adv;
  return w;
}

int iconFor(int code, bool day) {
  if (code == 0) return day ? IC_CLEAR_DAY : IC_CLEAR_NIGHT;
  if (code == 1 || code == 2) return day ? IC_PARTLY_DAY : IC_PARTLY_NIGHT;
  if (code == 3) return IC_CLOUDY;
  if (code == 45 || code == 48) return IC_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return IC_SNOW;
  if (code >= 95) return IC_STORM;
  if (code >= 51) return IC_RAIN;
  return IC_CLOUDY;
}
void icon(const ImgAsset* set, int kind, int x, int y) { g->drawPng(set[kind].data, set[kind].len, x, y); }

String f0(float v) { return isnan(v) ? String("-") : String((int)roundf(v)); }
String hh(int h) {
  char b[6];
  snprintf(b, sizeof(b), "%02d:00", ((h % 24) + 24) % 24);
  return b;
}

// ---------------- เฟรม ----------------
void beginFrame(const ImgAsset& bg) {
  if (canvas) {
    g = canvas;
    if (bgCache) {
      if (bgLoaded != &bg) {
        bgCache->drawJpg(bg.data, bg.len, 0, 0);
        bgLoaded = &bg;
      }
      bgCache->pushSprite(canvas, 0, 0);
    } else {
      canvas->drawJpg(bg.data, bg.len, 0, 0);
    }
  } else {
    g = gfx;
    gfx->startWrite();
    gfx->drawJpg(bg.data, bg.len, 0, 0);
  }
}
// ---------------- เร่งสี/ความสว่าง (ทำกับทุกพิกเซลก่อนส่งขึ้นจอ) ----------------
// ระดับ 0 = ปิด, 1 = เล็กน้อย, 2 = เข้ม-สด (ค่าเริ่มต้น), 3 = เข้มจัด
uint8_t vividLevel = VIVID_DEFAULT;
uint16_t* vividLut = nullptr;  // ตารางแปลงสี 65536 ค่า (อยู่ใน PSRAM)

void buildVivid() {
  if (vividLevel == 0) return;
  if (!vividLut) vividLut = (uint16_t*)ps_malloc(65536 * sizeof(uint16_t));
  if (!vividLut) return;
  // สีสดขึ้น + คอนทราสต์มากขึ้น + พื้นมืดดำลึกขึ้น (ภาพ "เข้ม" ไม่ซีด)
  static const float SAT[] = {1.0f, 1.3f, 1.5f, 1.7f}, CON[] = {1.0f, 1.15f, 1.25f, 1.35f}, GAM[] = {1.0f, 1.1f, 1.2f, 1.3f};
  float sat = SAT[vividLevel], con = CON[vividLevel], gam = GAM[vividLevel];
  // ทำตารางต่อช่องสีก่อน (เร็วกว่า powf ทุกค่า)
  for (uint32_t v = 0; v < 65536; v++) {
    uint16_t c = (uint16_t)((v & 0xFF) << 8 | v >> 8);  // sprite เก็บแบบสลับไบต์
    float r = ((c >> 11) & 31) / 31.0f, g6 = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
    float L = 0.299f * r + 0.587f * g6 + 0.114f * b;
    float ch[3] = {L + (r - L) * sat, L + (g6 - L) * sat, L + (b - L) * sat};
    for (auto& x : ch) {
      x = constrain(0.5f + (constrain(x, 0.0f, 1.0f) - 0.5f) * con, 0.0f, 1.0f);
      x = powf(x, gam);
    }
    uint16_t o = ((uint16_t)(ch[0] * 31 + 0.5f) << 11) | ((uint16_t)(ch[1] * 63 + 0.5f) << 5) | (uint16_t)(ch[2] * 31 + 0.5f);
    vividLut[v] = (uint16_t)((o & 0xFF) << 8 | o >> 8);
  }
}

void endFrame() {
  if (canvas) {
    if (vividLevel && vividLut) {
      uint16_t* p = (uint16_t*)canvas->getBuffer();
      for (int i = 0, n = canvas->width() * canvas->height(); i < n; i++) p[i] = vividLut[p[i]];
    }
    canvas->pushSprite(gfx, 0, 0);
  } else {
    gfx->endWrite();
  }
}

int page = 0;  // ประกาศล่วงหน้า (ค่าจริงอยู่ด้านล่าง)
const Settings* S = nullptr;
const Weather* Wx = nullptr;
const AirQuality* A = nullptr;
const Market* M = nullptr;
constexpr int PAGE_COUNT = 11;
enum { PAGE_HOME = 0, PAGE_SUM, PAGE_RADAR, PAGE_THRADAR, PAGE_STORM, PAGE_RAIN, PAGE_NOW, PAGE_FC, PAGE_AIR, PAGE_MARKET, PAGE_SKY };
void pageDots(int cx, int y) {
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = cx - (PAGE_COUNT - 1) * 4 + i * 8;
    if (i == page) g->fillSmoothCircle(x, y, 3, C_TEAL);
    else g->fillSmoothCircle(x, y, 1.6f, RGB(140, 150, 180));
  }
}
// แถบบนของทุกหน้า: วันที่ · อุณหภูมิตอนนี้ · เวลา (มีวินาที) + จุดบอกหน้าด้านล่างจอ
int tf(const TfSet& f, const String& s, int x, int y, uint32_t col, int align = 0, bool draw = true);
void headerClock(bool dots = true);
// แถบหัวของหน้าข้อมูล: ชื่อหน้า (ไทยคมชัด) + วันที่ อุณหภูมิ เวลา
void headerBar(const char* title) {
  tf(TF13B, title, 12, 17, C_GOLD);
  headerClock(true);
}
void headerClock(bool dots) {
  if (dots) pageDots(160, 236);
  struct tm t;
  bool ok = getLocalTime(&t, 0);
  char b[9] = "--:--:--";
  if (ok) snprintf(b, sizeof(b), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  txt(b, 310, 12, 11, C_WHITE, middle_right);
  int x = 310 - txtW(b, 11) - 8;
  if (Wx && Wx->valid && !isnan(Wx->temp)) {
    String tt = f0(Wx->temp) + "°";
    txt(tt, x, 12, 11, C_GOLD, middle_right);
    x -= txtW(tt, 11) + 8;
  }
  if (ok) {
    ThaiId mon = (ThaiId)(TH_MONS_0 + t.tm_mon), wd = (ThaiId)(TH_WDS_0 + t.tm_wday);
    thai(mon, x, 12, C_MUTED, 2);
    x -= thaiW(mon) + 3;
    String d = String(t.tm_mday);
    txt(d, x, 12, 11, C_WHITE, middle_right);
    x -= txtW(d, 11) + 3;
    thai(wd, x, 12, C_TEAL, 2);
  }
}

// ---------------- สถานะ ----------------

enum Screen { SCR_NONE, SCR_WEATHER, SCR_MESSAGE, SCR_SETUP } screen = SCR_NONE;
bool updating = false;
unsigned long lastFrame = 0, touchStart = 0, wakeUntil = 0;
bool longFired = false;
uint8_t brightNow = 0;

// ---------------- หน้า 1: นาฬิกา ----------------
void drawWifi(int x, int y) {
  bool ok = WiFi.status() == WL_CONNECTED;
  int rssi = ok ? WiFi.RSSI() : -100;
  int bars = rssi > -60 ? 3 : rssi > -72 ? 2 : rssi > -85 ? 1 : 0;
  for (int i = 0; i < 3; i++)
    g->fillSmoothRoundRect(x + i * 5, y - 3 - i * 3, 3, 4 + i * 3, 1, i < bars ? C_TEAL : RGB(70, 80, 100));
}

// ข้อมูลเรดาร์ล่าสุด (อ่านจาก task เรดาร์ไม่เกิน 1 ครั้ง/วินาที)
RadarInfo rinfo;
unsigned long rinfoAt = 0;
const RadarInfo& radarNow() {
  if (!rinfoAt || millis() - rinfoAt > 1000) { rinfo = Radar::info(); rinfoAt = millis(); }
  return rinfo;
}
// เรดาร์ใช้ได้ (มีภาพ ครอบคลุม และไม่เก่าเกิน 40 นาที)
bool radarFresh(const RadarInfo& r) {
  if (!r.ready || !r.covered || r.frames == 0) return false;
  time_t now = time(nullptr);
  return now < 1700000000 || now - (time_t)r.frameTime[r.frames - 1] < 40 * 60;
}
// ฝนจากพยากรณ์ราย 15 นาที: คืนนาทีที่จะเริ่มตก (0 = ตกอยู่), -1 = ไม่มีใน 2 ชม.
int nowMinute() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}
int minutesUntil(int slotMinute) {
  int nm = nowMinute();
  if (nm < 0) return 0;
  int d = slotMinute - nm;
  if (d < -720) d += 1440;
  if (d > 720) d -= 1440;
  return d;
}
int modelRainStart() {
  if (!Wx || Wx->mCount == 0) return -1;
  for (int i = 0; i < Wx->mCount; i++)
    if (Wx->mPrecip[i] >= 0.1f) {
      int d = minutesUntil(Wx->mMinute[i]);
      if (d > -15) return max(0, d);
    }
  return -1;
}
uint32_t rateColor(float mmh) {
  return mmh < 2.5f ? RGB(110, 200, 255) : mmh < 10 ? RGB(0, 140, 255) : mmh < 50 ? RGB(255, 170, 0) : RGB(255, 60, 60);
}
ThaiId rateName(float mmh) { return mmh < 2.5f ? TH_RN_LV0 : mmh < 10 ? TH_RN_LV1 : mmh < 50 ? TH_RN_LV2 : TH_RN_LV3; }
ThaiId dirName(int bearing) { return (ThaiId)(TH_DIR_0 + ((bearing + 22) % 360) / 45); }

void drawTicker() {
  const Weather& w = *Wx;
  int code = w.weatherCode;
  bool rainingNow = code >= 51 && code != 45 && code != 48 && code < 100;
  int at = -1;
  for (int i = 0; i < w.hCount && i < 12; i++)
    if (w.hRain[i] >= 60) { at = i; break; }
  uint32_t dot = C_TEAL;
  int x = 38, y = 212;
  const RadarInfo& r = radarNow();
  const RadarInfo* rd = radarFresh(r) ? &r : nullptr;
  int startIn = modelRainStart();
  if (updating) {
    thai(TH_T_UPDATING, x, y, C_MUTED);
  } else if (rd && rd->approaching && rd->etaMin > 0 && !isnan(rd->nearestKm)) {
    // แผนที่ฝนเห็นกลุ่มฝนกำลังเคลื่อนเข้ามา
    dot = C_FEEL;
    thai(TH_T_RADAR_A, x, y, RGB(255, 225, 200));
    x += thaiW(TH_T_RADAR_A) + 5;
    String km = String((int)roundf(rd->nearestKm));
    txt(km, x, y, 11, C_WHITE, middle_left);
    x += txtW(km, 11) + 5;
    thai(TH_T_RADAR_B, x, y, RGB(255, 225, 200));
    x += thaiW(TH_T_RADAR_B) + 5;
    txt(String(rd->etaMin), x, y, 11, C_GOLD, middle_left);
    x += txtW(String(rd->etaMin), 11) + 5;
    thai(TH_T_RADAR_C, x, y, RGB(255, 225, 200));
  } else if ((rd && rd->hereMmh >= 0.2f) || (!rd && rainingNow)) {
    dot = C_RAIN;
    thai(TH_T_RAIN_NOW, x, y, RGB(210, 230, 255));
  } else if (startIn > 0) {
    dot = C_RAIN;
    thai(TH_T_RAIN_IN, x, y, RGB(210, 230, 255));
    x += thaiW(TH_T_RAIN_IN) + 5;
    txt(String(startIn), x, y, 11, C_GOLD, middle_left);
    x += txtW(String(startIn), 11) + 5;
    thai(TH_T_RADAR_C, x, y, RGB(210, 230, 255));
  } else if (at >= 0) {
    dot = C_RAIN;
    thai(TH_T_RAIN_AT, x, y, RGB(210, 230, 255));
    x += thaiW(TH_T_RAIN_AT) + 5;
    txt(hh(w.hHour[at]), x, y, 11, C_WHITE, middle_left);
    x += txtW(hh(w.hHour[at]), 11) + 8;
    thai(TH_T_CHANCE, x, y, C_MUTED);
    x += thaiW(TH_T_CHANCE) + 4;
    txt(String(w.hRain[at]) + "%", x, y, 11, C_RAIN, middle_left);
  } else if (WeatherApi::heatLevel(w.feelsLike) >= 3) {
    dot = C_FEEL;
    thai(TH_T_HOT, x, y, RGB(255, 220, 200));
  } else if (A && A->valid && WeatherApi::pm25Level(A->pm25) >= 3) {
    dot = C_RED;
    thai(TH_T_PM, x, y, RGB(255, 210, 210));
  } else {
    thai(TH_T_NO_RAIN, x, y, RGB(210, 230, 255));
  }
  g->fillSmoothCircle(26, 212, 4, dot);
}

// จุด LIVE + เวลาที่อัปเดตล่าสุด
// คืนค่าขอบซ้ายที่ใช้ไป (ให้ข้อความที่อยู่ไม่ทับ)
int drawFreshness(int xr, int y) {
  if (!Wx || !Wx->valid || !Wx->fetchedAt) return xr;
  int mins = (millis() - Wx->fetchedAt) / 60000;
  int x = xr;
  if (mins >= 1) {
    x -= thaiW(TH_R_MIN);
    thai(TH_R_MIN, x, y, C_MUTED);
    String m = String(mins);
    x -= txtW(m, 9) + 2;
    txt(m, x, y, 9, C_WHITE, middle_left);
  }
  // จุดกะพริบแบบ LIVE
  float ph = (millis() % 1500) / 1500.0f;
  uint8_t a = 80 + (uint8_t)(175 * (0.5f + 0.5f * cosf(ph * 6.283f)));
  g->fillSmoothCircle(x - 8, y, 4, g->color888(0, a * 230 / 255, a));
  g->fillSmoothCircle(x - 8, y, 2, C_WHITE);
  return x - 14;
}

// ---------------- ข้อความไทยที่เปลี่ยนได้ (เรนเดอร์พอดีพิกเซล ไม่ย่อ/ขยาย = คมชัด) ----------------
const TfGlyph* tfFind(const TfSet& f, uint16_t cp, uint8_t kind) {
  for (int i = 0; i < f.n; i++) if (f.g[i].cp == cp && f.g[i].kind == kind) return &f.g[i];
  return nullptr;
}
void tfBlit(const TfGlyph* gl, int x, int y, uint32_t col) {
  if (!gl || !gl->data) return;
  uint8_t fr = col >> 16, fg = (col >> 8) & 0xFF, fb = col & 0xFF;
  int rb = (gl->w + 1) / 2;
  for (int yy = 0; yy < gl->h; yy++)
    for (int xx = 0; xx < gl->w; xx++) {
      uint8_t v = pgm_read_byte(gl->data + yy * rb + xx / 2);
      uint8_t a = (xx & 1) ? (v & 0x0F) : (v >> 4);
      if (!a) continue;
      int px = x + gl->dx + xx, py = y + gl->dy + yy;
      if (px < 0 || px >= 320 || py < 0 || py >= 240) continue;
      if (a == 15) { g->drawPixel(px, py, g->color888(fr, fg, fb)); continue; }
      auto b = g->readPixelRGB(px, py);
      g->drawPixel(px, py, g->color888(b.R8() + (fr - b.R8()) * a / 15, b.G8() + (fg - b.G8()) * a / 15, b.B8() + (fb - b.B8()) * a / 15));
    }
}
// วาดข้อความ UTF-8 ที่เส้นฐาน y; align 0 ซ้าย 1 กลาง 2 ขวา; คืนความกว้าง (draw=false = วัดอย่างเดียว)
int tf(const TfSet& f, const String& s, int x, int y, uint32_t col, int align, bool draw) {
  if (draw && align) {
    int w = tf(f, s, 0, 0, 0, 0, false);
    x -= align == 1 ? w / 2 : w;
  }
  int pen = x;
  bool upper = false;
  const uint8_t* p = (const uint8_t*)s.c_str();
  while (*p) {
    uint32_t cp;
    if (*p < 0x80) cp = *p++;
    else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
    else { p++; continue; }
    if (cp == 0x0E33) {  // ำ = นิคหิต + า
      if (draw) tfBlit(tfFind(f, 0x0E4D, 1), pen, y, col);
      cp = 0x0E32;
    }
    const TfGlyph* mk = tfFind(f, cp, 1);
    if (mk) {
      bool tone = cp >= 0x0E48 && cp <= 0x0E4C;
      const TfGlyph* hi = tone && upper ? tfFind(f, cp, 2) : nullptr;
      if (draw) tfBlit(hi ? hi : mk, pen, y, col);
      if (!tone && cp != 0x0E38 && cp != 0x0E39 && cp != 0x0E3A) upper = true;
      continue;
    }
    const TfGlyph* gl = tfFind(f, cp, 0);
    if (!gl) continue;
    if (draw) tfBlit(gl, pen, y, col);
    pen += gl->adv;
    upper = false;
  }
  return pen - x;
}
int tfW(const TfSet& f, const String& s) { return tf(f, s, 0, 0, 0, 0, false); }
int thaiText(const String& s, int x, int y, uint32_t col, bool draw = true) { return tf(TF12, s, x, y, col, 0, draw); }

// ---------------- กราฟิก: แสงเรือง เส้นจาง วงแหวนลื่น (ไม่มีกรอบสี่เหลี่ยม) ----------------
uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  return RGB(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t);
}
void blendPx(int x, int y, uint32_t col, uint8_t a) {
  if (x < 0 || y < 0 || x >= 320 || y >= 240 || !a) return;
  auto c = g->readPixelRGB(x, y);
  uint8_t r = col >> 16, gg = (col >> 8) & 255, b = col & 255;
  g->drawPixel(x, y, g->color888(c.R8() + (r - c.R8()) * a / 255, c.G8() + (gg - c.G8()) * a / 255, c.B8() + (b - c.B8()) * a / 255));
}
// แสงเรืองวงกลม (จางออกตามรัศมี)
void glow(int cx, int cy, int r, uint32_t col, int amax) {
  amax = constrain(amax, 0, 255);
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++) {
      float d = sqrtf((float)(x * x + y * y)) / r;
      if (d < 1) blendPx(cx + x, cy + y, col, (uint8_t)(amax * (1 - d) * (1 - d)));
    }
}
void fadeH(int x0, int x1, int y, uint32_t col = 0xA0C8FF, uint8_t amax = 80) {
  for (int x = x0; x < x1; x++) blendPx(x, y, col, (uint8_t)(amax * sinf(PI * (x - x0) / (float)(x1 - x0))));
}
void fadeV(int x, int y0, int y1, uint32_t col = 0xA0C8FF, uint8_t amax = 90) {
  for (int y = y0; y < y1; y++) blendPx(x, y, col, (uint8_t)(amax * sinf(PI * (y - y0) / (float)(y1 - y0))));
}
int cornerInset(int yy, int h, int r) {
  if (yy < r) return r - (int)sqrtf((float)(r * r - (r - yy) * (r - yy)));
  if (yy >= h - r) { int d = yy - (h - r - 1); return r - (int)sqrtf((float)max(0, r * r - d * d)); }
  return 0;
}
// สี่เหลี่ยมมุมมนโปร่งแสง (ใช้เป็นรางจาง ๆ)
void roundRectAlpha(int x, int y, int w, int h, int r, uint32_t col, uint8_t a) {
  for (int yy = 0; yy < h; yy++) {
    int in = cornerInset(yy, h, r);
    for (int xx = in; xx < w - in; xx++) blendPx(x + xx, y + yy, col, a);
  }
}
// แท่งไล่สีแนวตั้ง มุมมน
void gradBar(int x, int y, int w, int h, uint32_t top, uint32_t bot, int r) {
  r = min(r, min(w / 2, h / 2));
  for (int yy = 0; yy < h; yy++) {
    int in = cornerInset(yy, h, r);
    g->drawFastHLine(x + in, y + yy, w - 2 * in, mix(top, bot, h > 1 ? (float)yy / (h - 1) : 0));
  }
}
// เส้นโค้งลื่น (จุดวงกลมเรียบเรียงกัน) ไล่สี c1→c2, มุมตามเข็มนาฬิกาจาก 3 นาฬิกา
void smoothArc(int cx, int cy, int r, float w, float a0, float a1, uint32_t c1, uint32_t c2) {
  float len = (a1 - a0) * DEG_TO_RAD * r;
  int n = max(2, (int)(len / 1.2f));
  for (int i = 0; i <= n; i++) {
    float t = (float)i / n, a = (a0 + (a1 - a0) * t) * DEG_TO_RAD;
    g->fillSmoothCircle(cx + r * cosf(a), cy + r * sinf(a), w / 2, mix(c1, c2, t));
  }
}
void ring(int cx, int cy, int r, float w, float frac, uint32_t c1, uint32_t c2) {
  smoothArc(cx, cy, r, w, 135, 405, RGB(48, 52, 82), RGB(48, 52, 82));
  if (!isnan(frac) && frac > 0.005f) smoothArc(cx, cy, r, w, 135, 135 + 270 * constrain(frac, 0.0f, 1.0f), c1, c2);
}
void pill(int x, int y, int w, int h, uint32_t col) { g->fillSmoothRoundRect(x, y, w, h, h / 2, col); }

// ---------------- ปฏิทินจันทรคติไทย ----------------
const char* const WD_FULL[] = {"อาทิตย์", "จันทร์", "อังคาร", "พุธ", "พฤหัสบดี", "ศุกร์", "เสาร์"};
const char* const WD_SHORT[] = {"อา.", "จ.", "อ.", "พ.", "พฤ.", "ศ.", "ส."};
const char* const MON_FULL[] = {"มกราคม", "กุมภาพันธ์", "มีนาคม", "เมษายน", "พฤษภาคม", "มิถุนายน",
                                "กรกฎาคม", "สิงหาคม", "กันยายน", "ตุลาคม", "พฤศจิกายน", "ธันวาคม"};
const char* const MON_SHORT[] = {"ม.ค.", "ก.พ.", "มี.ค.", "เม.ย.", "พ.ค.", "มิ.ย.", "ก.ค.", "ส.ค.", "ก.ย.", "ต.ค.", "พ.ย.", "ธ.ค."};
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
struct Lunar { bool ok = false; int day = 0, len = 30, month = 0, animal = 0; };
Lunar lunarOf(int y, int m, int d) {
  Lunar L;
  long off = daysFromCivil(y, m, d) - daysFromCivil(2024, 12, 1);
  for (int i = 0; i + 1 < LUNAR_N; i++)
    if (off >= LUNAR[i].start && off < LUNAR[i + 1].start) {
      L.ok = true;
      L.day = off - LUNAR[i].start + 1;
      L.len = LUNAR[i + 1].start - LUNAR[i].start;
      L.month = LUNAR[i].month;
      L.animal = LUNAR[i].animal;
    }
  return L;
}
String lunarText(const Lunar& L) {
  String s = L.day <= 15 ? "ขึ้น " + String(L.day) : "แรม " + String(L.day - 15);
  s += " ค่ำ เดือน ";
  s += L.month == 88 ? String("8 หลัง") : String(L.month);
  return s;
}
bool isHoly(const Lunar& L) { return L.day == 8 || L.day == 15 || L.day == 23 || L.day == L.len; }

void drawHome() {
  beginFrame(BG_HOME);
  struct tm t;
  bool haveTime = getLocalTime(&t, 0);
  drawWifi(296, 15);
  int room = drawFreshness(284, 11) - 22;
  // ที่อยู่ ตำบล/อำเภอ/จังหวัด (ย่อลงถ้ายาวเกิน)
  String addr = S ? S->addrTh : String("");
  if (addr.length()) {
    g->fillSmoothCircle(15, 10, 3, C_RED);
    g->fillTriangle(12, 11, 18, 11, 15, 17, C_RED);
    g->fillSmoothCircle(15, 10, 1, C_WHITE);
    String a = addr;
    while (tfW(TF12, a) > room) {
      int sp = a.lastIndexOf(' ');
      if (sp <= 0) break;
      a = a.substring(0, sp);
    }
    tf(TF12, a, 22, 15, C_GOLD);
  } else {
    String city = S ? S->city : String("");
    int comma = city.indexOf(',');
    if (comma > 0) city = city.substring(0, comma);
    txt(city, 12, 11, 11, C_GOLD, middle_left);
  }
  char hm[6] = "--:--", sec[4] = ":--";
  if (haveTime) {
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(sec, sizeof(sec), ":%02d", t.tm_sec);
  }
  int x = drawGlyphs(CLOCK_GLYPHS, hm, 10, 4);
  drawGlyphs(SEC_GLYPHS, sec, x + 2, 4 + CLOCK_ASC - SEC_ASC);
  if (haveTime) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    float frac = (t.tm_sec + tv.tv_usec / 1e6f) / 60.0f;
    int w = (int)(292 * frac);
    if (w > 2) {
      for (int r = 0; r < 3; r++) g->drawGradientHLine(14, 90 + r, w, C_TEAL, C_GOLD);
      glow(14 + w, 91, 9, C_GOLD, 120);
      g->fillSmoothCircle(14 + w, 91, 5, C_GOLD);
      g->fillSmoothCircle(14 + w, 91, 2.5f, C_WHITE);
    }
    // วันที่ตัวใหญ่ (ชื่อเต็ม พ.ศ.)
    String date = String("วัน") + WD_FULL[t.tm_wday] + "ที่ " + t.tm_mday + " " + MON_FULL[t.tm_mon] + " " + (t.tm_year + 1900 + 543);
    if (tfW(TF17B, date) > 262)
      date = String("วัน") + WD_FULL[t.tm_wday] + " " + t.tm_mday + " " + MON_FULL[t.tm_mon] + " " + (t.tm_year + 1900 + 543);
    tf(TF17B, date, 12, 119, C_WHITE);
    // วันทางจันทรคติ + ปีนักษัตร + ป้ายวันพระ
    Lunar L = lunarOf(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    if (L.ok) {
      float ph = (L.day - 1 + 0.5f) / L.len;  // 0 = เดือนดับ, 0.5 = เต็มดวง
      const int mx = 19, my = 136, mr = 6;
      g->fillSmoothCircle(mx, my, mr, RGB(52, 56, 84));
      float c = cosf(2 * PI * ph);
      for (int y = -mr; y <= mr; y++) {
        float hw = sqrtf((float)(mr * mr - y * y)), a0, a1;
        if (ph < 0.5f) { a0 = hw * c; a1 = hw; } else { a0 = -hw; a1 = -hw * c; }
        if (a1 > a0) g->drawFastHLine(mx + (int)roundf(a0), my + y, max(1, (int)roundf(a1 - a0)), RGB(245, 238, 210));
      }
      int lx = 30 + tf(TF13B, lunarText(L), 30, 141, C_GOLD) + 8;
      lx += tf(TF12, String("ปี") + LUNAR_ANIMAL[L.animal], lx, 141, C_MUTED) + 8;
      if (isHoly(L) && lx < 250) {
        int pw = tfW(TF12, "วันพระ") + 14;
        pill(lx, 129, pw, 16, RGB(255, 170, 40));
        tf(TF12, "วันพระ", lx + pw / 2, 141, RGB(40, 20, 0), 1);
      }
    }
    if (Wx && Wx->valid) {
      txt(Wx->sunrise, 308, 113, 11, C_GOLD, middle_right);
      txt(Wx->sunset, 308, 130, 11, C_VIOLET, middle_right);
    }
  }
  if (Wx && Wx->valid) {
    const Weather& w = *Wx;
    icon(ICONS_M, iconFor(w.weatherCode, w.isDay), 6, 151);
    txt(f0(w.temp) + "°", 66, 168, 24, C_WHITE, middle_left);
    tf(TF10, WeatherApi::describe(w.weatherCode), 66, 196, C_MUTED);
    txt(f0(w.feelsLike) + "°", 160, 171, 14, C_FEEL, middle_center);
    txt(w.rainChance < 0 ? String("-") : String(w.rainChance) + "%", 200, 171, 14, C_RAIN, middle_center);
    if (A && A->valid) txt(f0(A->pm25), 240, 171, 14, pmColor(A->pm25), middle_center);
    float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
    txt(f0(uv), 280, 171, 14, uvColor(uv), middle_center);
    drawTicker();
  } else {
    thai(TH_NO_DATA, 160, 172, C_MUTED, 1);
  }
  pageDots(160, 234);
  endFrame();
}

// ---------------- หน้า 2: สรุปวันนี้ ----------------
void statItem(int i, const char* label, const String& value, uint32_t vcol, uint32_t dot, const String& sub, uint32_t scol) {
  int x = 14 + (i % 2) * 154, y = 36 + (i / 2) * 54;
  glow(x + 12, y + 16, 14, dot, 70);
  g->fillSmoothCircle(x + 12, y + 16, 5, dot);
  tf(TF10, label, x + 32, y + 10, C_MUTED);
  txt(value, x + 32, y + 26, 18, vcol, middle_left);
  if (sub.length()) tf(TF10, sub, x + 140, y + 46, scol, 2);
}
void drawSummary() {
  beginFrame(BG_PAGE);
  headerBar("สรุปวันนี้");
  if (!Wx || !Wx->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  for (int r = 0; r < 3; r++) {
    fadeV(160, 38 + r * 54, 84 + r * 54);
    if (r < 2) fadeH(14, 306, 87 + r * 54);
  }
  int hot = -1;
  for (int i = 0; i < w.hCount && i < 12; i++) if (hot < 0 || w.hTemp[i] > w.hTemp[hot]) hot = i;
  statItem(0, "อุณหภูมิ", f0(w.tempMin) + "° - " + f0(w.tempMax) + "°", C_WHITE, C_FEEL, hot >= 0 ? "ร้อนสุด " + hh(w.hHour[hot]) : String(""), C_MUTED);
  int r0 = -1, r1 = -1, rmax = 0;
  for (int i = 0; i < w.hCount && i < 12; i++) {
    rmax = max(rmax, w.hRain[i]);
    if (w.hRain[i] >= 50) { if (r0 < 0) r0 = i; r1 = i; }
  }
  if (r0 >= 0) statItem(1, "โอกาสฝน", String(rmax) + "%", C_RAIN, C_RAIN, hh(w.hHour[r0]) + "-" + hh(w.hHour[r1] + 1), C_RAIN);
  else statItem(1, "โอกาสฝน", String(max(0, rmax)) + "%", C_GREEN, C_RAIN, "ไม่น่าจะมีฝน", C_GREEN);
  if (A && A->valid) {
    int lv = WeatherApi::pm25Level(A->pm25);
    statItem(2, "ฝุ่น PM2.5", f0(A->pm25), pmColor(A->pm25), pmColor(A->pm25), lv >= 0 ? String(WeatherApi::pm25LevelName(lv)) : String(""), pmColor(A->pm25));
  } else {
    statItem(2, "ฝุ่น PM2.5", "-", C_MUTED, C_GREEN, "", C_MUTED);
  }
  int s0 = -1, s1 = -1;
  for (int i = 0; i < w.hCount; i++) {
    if (!isnan(w.hUv[i]) && w.hUv[i] >= 6) { if (s0 < 0) s0 = i; s1 = i; }
    else if (s0 >= 0) break;
  }
  statItem(3, "รังสี UV", isnan(w.uvMax) ? String("-") : String(w.uvMax, 1), uvColor(w.uvMax), C_UV,
           s0 >= 0 ? "แดดแรง " + hh(w.hHour[s0]) + "-" + hh(w.hHour[s1] + 1) : String("แดดไม่แรง"), s0 >= 0 ? C_MUTED : C_GREEN);
  statItem(4, "ดวงอาทิตย์ขึ้น", w.sunrise, C_GOLD, C_GOLD, "ตก " + w.sunset, C_VIOLET);
  statItem(5, "ความชื้น · ลม", w.humidity < 0 ? String("-") : String(w.humidity) + "%", C_TEAL, C_TEAL, "ลม " + f0(w.windSpeed) + " กม./ชม.", C_MUTED);
  // คำแนะนำวันนี้ (ป้ายสี)
  struct Adv { const char* t; uint32_t c; } adv[5];
  int n = 0;
  if (r0 >= 0 || w.rainChance >= 50) adv[n++] = {"พกร่ม", RGB(0, 120, 255)};
  if (!isnan(w.uvMax) && w.uvMax >= 6) adv[n++] = {"ทากันแดด", RGB(220, 60, 190)};
  if (A && A->valid && WeatherApi::pm25Level(A->pm25) >= 2) adv[n++] = {"ใส่หน้ากาก", RGB(230, 70, 80)};
  if (WeatherApi::heatLevel(w.feelsLike) >= 2) adv[n++] = {"ดื่มน้ำบ่อย ๆ", RGB(230, 120, 40)};
  if (n == 0) adv[n++] = {"อากาศดี ออกไปข้างนอกได้", RGB(30, 160, 90)};
  int ax = 12;
  for (int i = 0; i < n; i++) {
    int wdt = tfW(TF12, adv[i].t) + 20;
    if (ax + wdt > 312) break;
    pill(ax, 204, wdt, 20, adv[i].c);
    tf(TF12, adv[i].t, ax + wdt / 2, 218, C_WHITE, 1);
    ax += wdt + 6;
  }
  endFrame();
}

// ---------------- หน้า 3: เรดาร์ฝนเต็มจอ ----------------
// เส้นโปร่งแสง (ผสมกับภาพด้านล่าง)
void blendLine(float x0, float y0, float x1, float y1, uint32_t col, uint8_t a) {
  int n = (int)max(fabsf(x1 - x0), fabsf(y1 - y0));
  uint8_t r = col >> 16, gg = (col >> 8) & 0xFF, b = col & 0xFF;
  for (int i = 0; i <= n; i++) {
    int x = (int)roundf(x0 + (x1 - x0) * i / max(1, n)), y = (int)roundf(y0 + (y1 - y0) * i / max(1, n));
    if (x < 0 || y < 0 || x >= 320 || y >= 240) continue;
    auto c = g->readPixelRGB(x, y);
    g->drawPixel(x, y, g->color888(c.R8() + (r - c.R8()) * a / 255, c.G8() + (gg - c.G8()) * a / 255, c.B8() + (b - c.B8()) * a / 255));
  }
}

// ต่อข้อความไทย/ตัวเลขเป็นแถวเดียว
struct Line {
  int x, y;
  void th(ThaiId id, uint32_t c, int gap = 4) { thai(id, x, y, c); x += thaiW(id) + gap; }
  void num(const String& s, int px, uint32_t c, int gap = 4) { txt(s, x, y, px, c, middle_left); x += txtW(s, px) + gap; }
};

void drawRadar() {
  const RadarInfo& r = radarNow();
  if (!canvas || !r.ready) {
    beginFrame(BG_PLAIN);
    headerClock();
    if (!Radar::started()) {
      tf(TF13B, "เรดาร์ปิดไว้ชั่วคราว", 160, 104, C_GOLD, 1);
      tf(TF10, "บอร์ดเพิ่งรีสตาร์ทผิดปกติ จึงเข้าโหมดปลอดภัย", 160, 128, C_MUTED, 1);
      tf(TF10, "เรดาร์จะเปิดเองเมื่อทำงานปกติครบ 10 นาที", 160, 146, C_MUTED, 1);
    } else if (!r.covered) {
      thai(TH_R_NOCOV, 160, 120, C_MUTED, 1);
    } else {
      thai(TH_R_NODATA, 160, 104, C_MUTED, 1);
      String st = r.phase == 1 ? String("กำลังโหลดแผนที่พื้นที่ของคุณ") : r.phase == 2 ? "กำลังโหลดภาพเรดาร์ " + String(r.progress) + "/" + RADAR_FRAMES
                                                                     : String("กำลังเชื่อมต่อเซิร์ฟเวอร์เรดาร์");
      tf(TF12, st, 160, 136, C_TEAL, 1);
      if (r.error.length()) txt(r.error, 160, 158, 9, C_MUTED, middle_center);
    }
    endFrame();
    return;
  }
  g = canvas;
  unsigned long now = millis();
  // เล่นภาพย้อนหลังทีละเฟรม (0.5 วินาที) แล้วค้างภาพล่าสุด 2 วินาที
  int n = r.frames, cycle = (n - 1) * 500 + 2000;
  int t = now % cycle;
  int fi = min(n - 1, t / 500);
  Radar::drawFrame(fi, canvas);
  const int cx = 160, cy = 120;
  // วงรัศมี 10 / 25 / 50 กม.
  for (int km : {10, 25, 50}) {
    int rr = (int)(km / r.kmPerPx);
    g->drawCircle(cx, cy, rr, RGB(80, 130, 160));
    txt(String(km), cx + 2, cy - rr + 1, 9, RGB(170, 210, 235), top_left);
  }
  // เส้นกวาดเรดาร์
  float ang = (now % 4000) / 4000.0f * 6.2831853f;
  for (int k = 12; k >= 0; k--) {
    float a = ang - k * 0.04f;
    blendLine(cx, cy, cx + 118 * sinf(a), cy - 118 * cosf(a), C_TEAL, (uint8_t)(130 - k * 10));
  }
  // ตำแหน่งของเรา
  float pulse = (now % 1600) / 1600.0f;
  g->drawCircle(cx, cy, 4 + (int)(pulse * 14), g->color888(255 * (1 - pulse), 214 * (1 - pulse), 120 * (1 - pulse)));
  g->fillSmoothCircle(cx, cy, 5, C_GOLD);
  g->fillSmoothCircle(cx, cy, 2, RGB(20, 20, 20));
  // แถบบน: ชื่อ + เวลาเฟรม + เส้นเวลา + นาฬิกา
  g->fillRectAlpha(0, 0, 320, 22, 160, 0);
  thai(TH_R_TITLE, 8, 11, C_GOLD);
  int tx = 8 + thaiW(TH_R_TITLE) + 6;
  time_t ft = r.frameTime[fi];
  struct tm lt;
  localtime_r(&ft, &lt);
  char hm[6];
  snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
  txt(hm, tx, 11, 11, fi == n - 1 ? C_WHITE : C_MUTED, middle_left);
  tx += txtW(hm, 11) + 8;
  for (int i = 0; i < n; i++) g->fillSmoothRoundRect(tx + i * 7, 8, 5, 6, 2, i == fi ? C_TEAL : RGB(70, 85, 110));
  if (fi == n - 1) {
    bool on = (now / 500) % 2;
    g->fillSmoothCircle(tx + n * 7 + 4, 11, 3, on ? C_RED : RGB(120, 40, 50));
  }
  headerClock(false);
  // เครดิต
  txt("Esri | RainViewer", 316, 204, 9, RGB(150, 160, 180), bottom_right);
  // แถบล่าง: สถานะฝน
  g->fillRectAlpha(0, 208, 320, 32, 170, 0);
  pageDots(160, 236);
  Line L{10, 222};
  if (!r.covered) {
    L.th(TH_R_NOCOV, C_MUTED);
  } else if (r.hereMmh >= 0.2f) {
    g->fillSmoothCircle(L.x + 4, L.y, 4, rateColor(r.hereMmh));
    L.x += 12;
    L.th(TH_R_HERE, C_WHITE, 8);
    L.num(String(r.hereMmh, 1), 14, rateColor(r.hereMmh));
    L.th(TH_R_MMH, C_MUTED, 8);
    L.th(rateName(r.hereMmh), rateColor(r.hereMmh));
  } else if (r.approaching && r.etaMin > 0 && !isnan(r.nearestKm)) {
    float bl = 0.5f + 0.5f * sinf(now / 250.0f);
    g->fillSmoothCircle(L.x + 4, L.y, 4, g->color888(255, 90 + 80 * bl, 40));
    L.x += 12;
    L.th(TH_T_RADAR_A, RGB(255, 225, 200));
    L.num(String((int)roundf(r.nearestKm)), 14, C_WHITE, 3);
    L.th(TH_R_KM, C_MUTED, 6);
    if (r.bearing >= 0) L.th(dirName(r.bearing), C_RAIN, 8);
    L.th(TH_R_ETA2, RGB(255, 225, 200), 2);
    L.num(String(r.etaMin), 14, C_GOLD, 3);
    L.th(TH_R_MIN, C_MUTED);
  } else if (!isnan(r.nearestKm)) {
    g->fillSmoothCircle(L.x + 4, L.y, 4, C_RAIN);
    L.x += 12;
    L.th(TH_R_NEAR, C_WHITE, 6);
    L.num(String((int)roundf(r.nearestKm)), 14, C_WHITE, 3);
    L.th(TH_R_KM, C_MUTED, 8);
    if (r.bearing >= 0) L.th(dirName(r.bearing), C_RAIN);
  } else {
    g->fillSmoothCircle(L.x + 4, L.y, 4, C_GREEN);
    L.x += 12;
    L.th(TH_R_NONE, C_GREEN, 6);
    L.th(TH_R_NONE2, C_WHITE);
  }
  endFrame();
}

// ---------------- หน้า 4-5: เรดาร์ฝนทั้งประเทศ / ดาวเทียมพายุ ----------------
double worldX(double lon, int z) { return (lon + 180.0) / 360.0 * 256.0 * (1 << z); }
double worldY(double lat, int z) {
  double sn = sin(lat * DEG_TO_RAD);
  return (0.5 - log((1 + sn) / (1 - sn)) / (4 * PI)) * 256.0 * (1 << z);
}
float distKm(double la1, double lo1, double la2, double lo2) {
  double dla = (la2 - la1) * DEG_TO_RAD, dlo = (lo2 - lo1) * DEG_TO_RAD;
  double a = sin(dla / 2) * sin(dla / 2) + cos(la1 * DEG_TO_RAD) * cos(la2 * DEG_TO_RAD) * sin(dlo / 2) * sin(dlo / 2);
  return 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}
int bearingDeg(double la1, double lo1, double la2, double lo2) {
  double y = sin((lo2 - lo1) * DEG_TO_RAD) * cos(la2 * DEG_TO_RAD);
  double x = cos(la1 * DEG_TO_RAD) * sin(la2 * DEG_TO_RAD) - sin(la1 * DEG_TO_RAD) * cos(la2 * DEG_TO_RAD) * cos((lo2 - lo1) * DEG_TO_RAD);
  return ((int)lround(atan2(y, x) * RAD_TO_DEG) + 360) % 360;
}
void drawMapPage(int which) {
  MapInfo mi = Radar::mapInfo(which);
  const char* title = which == 0 ? "ฝนทั่วไทย" : "พายุ · ดาวเทียม";
  if (!canvas || !mi.ok) {
    beginFrame(BG_PAGE);
    headerBar(title);
    tf(TF13B, Radar::started() ? "กำลังโหลดภาพ..." : "ปิดไว้ชั่วคราว (โหมดปลอดภัย)", 160, 120, C_MUTED, 1);
    tf(TF10, "ภาพอัปเดตทุก 10 นาที", 160, 142, C_MUTED, 1);
    endFrame();
    return;
  }
  g = canvas;
  unsigned long now = millis();
  int n = mi.frames, cycle = (n - 1) * 700 + 2200;
  int fi = min(n - 1, (int)(now % cycle) / 700);
  Radar::drawMap(which, fi, canvas);
  // หมุดตำแหน่งของบอร์ด
  if (S && !isnan(S->lat)) {
    int px = (worldX(S->lon, mi.z) - mi.x0) * mi.scale, py = (worldY(S->lat, mi.z) - mi.y0) * mi.scale;
    if (px > 0 && px < 320 && py > 22 && py < 206) {
      float pl = (now % 1600) / 1600.0f;
      g->drawCircle(px, py, 4 + (int)(pl * 12), mix(C_GOLD, RGB(0, 0, 0), pl));
      g->fillSmoothCircle(px, py, 4, C_GOLD);
      g->fillSmoothCircle(px, py, 1.6f, RGB(30, 20, 0));
    }
  }
  // แถบบน: ชื่อ + เวลาภาพ + เส้นเวลา + วันที่/เวลา
  g->fillRectAlpha(0, 0, 320, 22, 165, 0);
  int tx = 8 + tf(TF13B, title, 8, 16, C_GOLD) + 6;
  time_t ft = mi.ts[fi];
  struct tm lt;
  localtime_r(&ft, &lt);
  char hm[6];
  snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
  txt(hm, tx, 11, 11, fi == n - 1 ? C_WHITE : C_MUTED, middle_left);
  tx += txtW(hm, 11) + 6;
  for (int i = 0; i < n; i++) g->fillSmoothRoundRect(tx + i * 7, 8, 5, 6, 2, i == fi ? C_TEAL : RGB(70, 85, 110));
  headerClock(false);
  // แถบล่าง
  g->fillRectAlpha(0, 206, 320, 34, 175, 0);
  pageDots(160, 236);
  if (which == 0) {
    // คำอธิบายสีฝน
    const uint32_t lc[] = {RGB(0x36, 0xba, 0xe5), RGB(0x00, 0x77, 0xaa), RGB(0xff, 0xc5, 0x00), RGB(0xf2, 0x36, 0x00), RGB(0xff, 0x6c, 0xff)};
    const char* ln[] = {"เบา", "ปานกลาง", "หนัก", "หนักมาก", "รุนแรง"};
    int x = 10;
    for (int i = 0; i < 5; i++) {
      g->fillSmoothRoundRect(x, 213, 12, 8, 2, lc[i]);
      x += 15 + tf(TF10, ln[i], x + 15, 222, C_MUTED) + 8;
    }
    txt("RainViewer | Esri", 312, 198, 9, RGB(160, 170, 195), middle_right);
  } else {
    StormInfo st[8];
    int sn = Radar::storms(st, 8);
    int best = -1;
    float bd = 1e9;
    for (int i = 0; i < sn; i++) {
      float d = S ? distKm(S->lat, S->lon, st[i].lat, st[i].lon) : 0;
      if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) {
      g->fillSmoothCircle(14, 218, 4, C_GREEN);
      tf(TF12, "ไม่มีพายุหมุนเขตร้อนในภูมิภาค", 22, 222, C_GREEN);
    } else {
      const StormInfo& s = st[best];
      uint32_t c = s.wind >= 118 ? C_RED : s.wind >= 63 ? RGB(255, 170, 40) : RGB(255, 230, 90);
      const char* cat = s.wind >= 118 ? "ไต้ฝุ่น" : s.wind >= 63 ? "พายุโซนร้อน" : "พายุดีเปรสชัน";
      float bl = 0.5f + 0.5f * sinf(now / 250.0f);
      g->fillSmoothCircle(14, 218, 4, mix(c, C_WHITE, bl * 0.4f));
      String nm = String(s.name);
      int dash = nm.indexOf('-');
      if (dash > 0) nm = nm.substring(0, dash);
      int x = 22 + tf(TF12, String(cat) + " " + nm, 22, 222, c) + 8;
      x += tf(TF10, "ลม " + String(s.wind) + " กม./ชม.", x, 222, C_WHITE) + 8;
      if (S && x < 230) {
        static const char* const dn[] = {"ทิศเหนือ", "ทิศตะวันออกเฉียงเหนือ", "ทิศตะวันออก", "ทิศตะวันออกเฉียงใต้", "ทิศใต้", "ทิศตะวันตกเฉียงใต้", "ทิศตะวันตก", "ทิศตะวันตกเฉียงเหนือ"};
        String far = "ห่าง " + String((int)lroundf(bd)) + " กม.";
        x += tf(TF10, far, x, 222, C_MUTED) + 6;
        if (x + tfW(TF10, dn[((bearingDeg(S->lat, S->lon, s.lat, s.lon) + 22) % 360) / 45]) < 314)
          tf(TF10, dn[((bearingDeg(S->lat, S->lon, s.lat, s.lon) + 22) % 360) / 45], x, 222, C_MUTED);
      }
    }
    // คำอธิบายสีเมฆ: สีสด = ยอดเมฆสูง/พายุฝนฟ้าคะนอง
    const uint32_t ic[] = {RGB(150, 150, 160), RGB(0, 200, 255), RGB(0, 220, 80), RGB(255, 230, 0), RGB(255, 40, 30)};
    for (int i = 0; i < 5; i++) g->fillRect(242 + i * 14, 197, 14, 5, ic[i]);
    txt("NASA Himawari | GDACS", 312, 189, 9, RGB(160, 170, 195), middle_right);
  }
  endFrame();
}

// ---------------- หน้า 4: ฝนใน 2 ชั่วโมง ----------------
void drawRain() {
  beginFrame(BG_PAGE);
  headerBar("ฝน 2 ชม. ข้างหน้า");
  if (!Wx || !Wx->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  const RadarInfo& rr = radarNow();
  const RadarInfo* rd = radarFresh(rr) ? &rr : nullptr;
  float total = 0, peak = 0;
  for (int i = 0; i < w.mCount; i++) { total += w.mPrecip[i]; peak = max(peak, w.mPrecip[i] * 4); }
  int start = modelRainStart();
  bool rainingNow = (rd && rd->hereMmh >= 0.2f) || (!rd && start == 0);
  int eta = start;
  bool fromRadar = false;
  if (!rainingNow && rd && rd->approaching && rd->etaMin > 0 && (eta < 0 || rd->etaMin < eta)) { eta = rd->etaMin; fromRadar = true; }
  // บรรทัดหลัก
  if (rainingNow) {
    float bl = 0.5f + 0.5f * sinf(millis() / 300.0f);
    glow(20, 46, 12, C_RAIN, 90 + 60 * bl);
    g->fillSmoothCircle(20, 46, 6, C_RAIN);
    tf(TF17B, "ฝนกำลังตกอยู่", 32, 52, C_WHITE);
    for (int i = 0; i < w.mCount; i++)
      if (minutesUntil(w.mMinute[i]) > 0 && w.mPrecip[i] < 0.1f) {
        char b[6];
        snprintf(b, sizeof(b), "%02d:%02d", w.mMinute[i] / 60, w.mMinute[i] % 60);
        txt(b, 306, 47, 14, C_GOLD, middle_right);
        tf(TF10, "คาดว่าหยุดประมาณ", 306 - txtW(b, 14) - 6, 51, C_MUTED, 2);
        break;
      }
  } else if (eta > 0) {
    uint32_t c = fromRadar ? C_FEEL : C_RAIN;
    glow(20, 46, 12, c, 110);
    g->fillSmoothCircle(20, 46, 6, c);
    int x = 32 + tf(TF17B, "ฝนจะเริ่มตกในอีก", 32, 52, C_WHITE) + 8;
    glow(x + txtW(String(eta), 30) / 2, 46, 20, C_GOLD, 50);
    txt(String(eta), x, 46, 30, C_GOLD, middle_left);
    x += txtW(String(eta), 30) + 6;
    tf(TF12, "นาที", x, 52, C_MUTED);
    if (fromRadar) tf(TF10, "จากเรดาร์", 306, 51, C_FEEL, 2);
  } else {
    glow(20, 46, 12, C_GREEN, 90);
    g->fillSmoothCircle(20, 46, 6, C_GREEN);
    tf(TF17B, "ไม่มีฝนใน 2 ชั่วโมงนี้", 32, 52, C_GREEN);
  }
  // ปริมาณรวม + ระดับ + เรดาร์ตอนนี้
  int x = 16 + tf(TF10, "รวม 2 ชม.", 16, 78, C_MUTED) + 6;
  txt(String(total, 1), x, 74, 18, total >= 0.1f ? C_RAIN : C_WHITE, middle_left);
  x += txtW(String(total, 1), 18) + 4;
  x += tf(TF10, "มม.", x, 78, C_MUTED) + 10;
  if (peak >= 0.4f) {
    String lv = peak < 2.5f ? "ฝนเล็กน้อย" : peak < 10 ? "ฝนปานกลาง" : peak < 50 ? "ฝนหนัก" : "ฝนหนักมาก";
    int pw = tfW(TF12, lv) + 16;
    pill(x, 66, pw, 18, rateColor(peak));
    tf(TF12, lv, x + pw / 2, 79, RGB(10, 20, 40), 1);
  }
  if (rd) {
    String v = String(rd->hereMmh, 1);
    txt(v, 306, 74, 11, rd->hereMmh >= 0.2f ? rateColor(rd->hereMmh) : C_WHITE, middle_right);
    tf(TF10, "เรดาร์ตอนนี้", 306 - txtW(v, 11) - 6, 78, C_MUTED, 2);
  }
  // แท่งทุก 15 นาที (ไล่สีเรืองแสง ไม่มีกรอบ)
  if (w.mCount > 0) {
    float scale = max(0.6f, peak / 4);
    const int base = 150, maxH = 52;
    for (int i = 0; i < w.mCount; i++) {
      int cx = 36 + i * 36;
      float v = w.mPrecip[i];
      roundRectAlpha(cx - 12, base - maxH, 24, maxH, 6, RGB(255, 255, 255), 14);
      int h = (int)(v / scale * maxH);
      if (v >= 0.05f) {
        h = max(h, 4);
        glow(cx, base - h / 2, 18, rateColor(v * 4), 40);
        gradBar(cx - 12, base - h, 24, h, mix(rateColor(v * 4), C_WHITE, 0.35f), rateColor(v * 4), 6);
        txt(String(v, 1), cx, base - h - 8, 9, C_WHITE, middle_center);
      }
      char b[6];
      snprintf(b, sizeof(b), "%02d:%02d", w.mMinute[i] / 60, w.mMinute[i] % 60);
      txt(b, cx, 160, 9, i == 0 ? C_GOLD : C_MUTED, middle_center);
    }
  }
  fadeH(12, 308, 172);
  tf(TF10, "12 ชั่วโมงข้างหน้า (มม.)", 306, 186, C_MUTED, 2);
  // กราฟคลื่น 12 ชั่วโมง
  int hn = min(w.hCount, 12);
  float hmax = 1;
  for (int i = 0; i < hn; i++) if (!isnan(w.hPrecip[i])) hmax = max(hmax, w.hPrecip[i]);
  auto PX = [&](int i) { return 20 + i * 25.5f; };
  auto PY = [&](int i) { return 220 - (isnan(w.hPrecip[i]) ? 0 : w.hPrecip[i]) / hmax * 26; };
  for (int i = 0; i + 1 < hn; i++)
    for (int xx = (int)PX(i); xx < (int)PX(i + 1); xx++) {
      float t = (xx - PX(i)) / 25.5f, yy = PY(i) + (PY(i + 1) - PY(i)) * t;
      for (int y = (int)yy; y < 220; y++) blendPx(xx, y, C_RAIN, (uint8_t)(90 - (y - yy) * 60 / max(1.0f, 220 - yy)));
    }
  for (int i = 0; i + 1 < hn; i++) g->drawWideLine(PX(i), PY(i), PX(i + 1), PY(i + 1), 1.4f, RGB(140, 205, 255));
  int best = -1;
  for (int i = 0; i < hn; i++) if (!isnan(w.hPrecip[i]) && w.hPrecip[i] >= 0.1f && (best < 0 || w.hPrecip[i] > w.hPrecip[best])) best = i;
  if (best >= 0) {
    g->fillSmoothCircle(PX(best), PY(best), 3, C_WHITE);
    txt(String(w.hPrecip[best], 1), PX(best), PY(best) - 8, 9, C_WHITE, middle_center);
  }
  for (int i = 0; i < hn; i += 3) txt(hh(w.hHour[i]), PX(i), 229, 9, C_MUTED, middle_center);
  endFrame();
}

// ---------------- หน้า 7: คุณภาพอากาศ ----------------
uint32_t aqiColor(int v) {
  return v <= 50 ? RGB(130, 240, 160) : v <= 100 ? RGB(250, 220, 90) : v <= 150 ? RGB(255, 150, 80) : v <= 200 ? RGB(255, 90, 110) : RGB(200, 110, 255);
}
const char* aqiName(int v) {
  return v <= 50 ? "ดี" : v <= 100 ? "ปานกลาง" : v <= 150 ? "กลุ่มเสี่ยงระวัง" : v <= 200 ? "ไม่ดีต่อสุขภาพ" : "อันตราย";
}
// เกณฑ์ PM10 กรมควบคุมมลพิษ
int pm10Level(float v) { return isnan(v) ? -1 : v <= 50 ? 0 : v <= 80 ? 1 : v <= 120 ? 2 : v <= 180 ? 3 : 4; }
void drawAir() {
  beginFrame(BG_PAGE);
  headerBar("คุณภาพอากาศ");
  if (!A || !A->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const AirQuality& a = *A;
  // เกจครึ่งวงกลม 5 ระดับ (กรมควบคุมมลพิษ)
  const int cx = 92, cy = 112, r = 62;
  const float seg[] = {0, 15, 25, 37.5f, 75, 100};
  const uint32_t segc[] = {RGB(90, 210, 255), RGB(130, 240, 160), RGB(250, 220, 90), RGB(255, 140, 90), RGB(230, 90, 200)};
  for (int i = 0; i < 5; i++) smoothArc(cx, cy, r, 8, 180 + seg[i] * 1.8f + 1.5f, 180 + seg[i + 1] * 1.8f - 1.5f, segc[i], segc[i]);
  float v = constrain(a.pm25, 0.0f, 100.0f);
  float ang = (180 + v * 1.8f) * DEG_TO_RAD;
  int mx = cx + r * cosf(ang), my = cy + r * sinf(ang);
  glow(mx, my, 12, C_WHITE, 120);
  g->fillSmoothCircle(mx, my, 7, C_WHITE);
  g->fillSmoothCircle(mx, my, 4, pmColor(a.pm25));
  uint32_t pc = pmColor(a.pm25);
  glow(cx, cy - 22, 30, pc, 40);
  txt(f0(a.pm25), cx, cy - 20, 30, pc, middle_center);
  tf(TF10, "PM2.5 มคก./ลบ.ม.", cx, cy + 6, C_MUTED, 1);
  int lv = WeatherApi::pm25Level(a.pm25);
  if (lv >= 0) {
    String n = WeatherApi::pm25LevelName(lv);
    int pw = tfW(TF12, n) + 18;
    pill(cx - pw / 2, cy + 14, pw, 18, pc);
    tf(TF12, n, cx, cy + 27, RGB(20, 24, 36), 1);
  }
  // PM10 / AQI
  int l10 = pm10Level(a.pm10);
  uint32_t c10 = l10 < 0 ? C_MUTED : segc[l10];
  ring(206, 58, 18, 4, a.pm10 / 180.0f, c10, c10);
  txt(f0(a.pm10), 206, 58, 11, C_WHITE, middle_center);
  txt("PM10", 232, 52, 9, C_MUTED, middle_left);
  if (l10 >= 0) tf(TF12, WeatherApi::pm25LevelName(l10), 232, 72, c10);
  uint32_t ca = a.usAqi < 0 ? C_MUTED : aqiColor(a.usAqi);
  ring(206, 116, 18, 4, a.usAqi / 300.0f, ca, ca);
  txt(a.usAqi < 0 ? String("-") : String(a.usAqi), 206, 116, 11, C_WHITE, middle_center);
  txt("AQI (US)", 232, 110, 9, C_MUTED, middle_left);
  if (a.usAqi >= 0) tf(TF12, aqiName(a.usAqi), 232, 130, ca);
  fadeH(12, 308, 166);
  tf(TF10, "PM2.5 ล่วงหน้า 12 ชม.", 306, 179, C_MUTED, 2);
  float mxv = 50;
  for (int i = 0; i < a.hPmCount; i++) mxv = max(mxv, a.hPm[i]);
  for (int i = 0; i < a.hPmCount; i++) {
    int x = 24 + i * 24;
    int h = max(4, (int)(a.hPm[i] / mxv * 36));
    uint32_t c = pmColor(a.hPm[i]);
    glow(x, 216 - h / 2, 12, c, 30);
    gradBar(x - 6, 216 - h, 12, h, mix(c, C_WHITE, 0.3f), c, 5);
    if (i % 3 == 0) txt(hh(a.hPmHour[i]), x, 226, 9, C_MUTED, middle_center);
  }
  endFrame();
}

// ---------------- หน้า 8: ทองคำ · หุ้น ----------------
String thousands(float v, int dec = 0) {
  if (isnan(v)) return "-";
  String s = String(fabsf(v), dec);
  int dot = s.indexOf('.');
  int end = dot < 0 ? s.length() : dot;
  for (int i = end - 3; i > 0; i -= 3) s = s.substring(0, i) + "," + s.substring(i);
  return v < 0 ? "-" + s : s;
}
String signedStr(float v, int dec) { return isnan(v) ? String("-") : (v >= 0 ? "+" : "") + String(v, dec); }
void arrow(int x, int y, bool up, uint32_t c) {
  if (up) g->fillTriangle(x, y + 4, x + 10, y + 4, x + 5, y - 4, c);
  else g->fillTriangle(x, y - 4, x + 10, y - 4, x + 5, y + 4, c);
}
void drawMarket() {
  beginFrame(BG_PAGE);
  headerBar("ทองคำ · หุ้นไทย");
  if (!M || !M->valid) {
    thai(TH_MK_NODATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Market& m = *M;
  if (m.goldValid) {
    glow(60, 80, 46, C_GOLD, 45);
    int x = 14 + tf(TF12, "ทองคำแท่ง 96.5%", 14, 43, C_GOLD) + 6;
    if (m.goldTime.length()) txt(m.goldTime, x, 39, 9, C_MUTED, middle_left);
    if (m.goldEstimate) tf(TF10, "(ประมาณ)", x + 34, 43, C_MUTED);
    tf(TF10, "ขายออก", 14, 63, C_MUTED);
    String sell = thousands(m.barSell);
    txt(sell, 14, 81, 30, C_GOLD, middle_left);
    if (!isnan(m.goldChange) && m.goldChange != 0) {
      bool up = m.goldChange > 0;
      uint32_t c = up ? C_GREEN : C_RED;
      int ax = 14 + txtW(sell, 30) + 10;
      arrow(ax, 81, up, c);
      txt(signedStr(m.goldChange, 0), ax + 14, 81, 14, c, middle_left);
    }
    x = 14 + tf(TF10, "รับซื้อ", 14, 109, C_MUTED) + 6;
    txt(thousands(m.barBuy), x, 105, 14, C_WHITE, middle_left);
    x = 14 + tf(TF10, "รูปพรรณ ขาย", 14, 125, C_MUTED) + 6;
    txt(thousands(m.ornSell), x, 121, 11, C_MUTED, middle_left);
  }
  fadeV(160, 34, 126);
  tf(TF10, "ดอลลาร์ (บาท)", 172, 47, C_MUTED);
  txt(isnan(m.usdthb) ? String("-") : String(m.usdthb, 2), 172, 63, 18, C_WHITE, middle_left);
  tf(TF10, "ทองคำโลก", 172, 89, C_MUTED);
  txt(isnan(m.xau) ? String("-") : "$" + thousands(m.xau), 172, 105, 18, C_WHITE, middle_left);
  if (!isnan(m.xauPct)) txt(signedStr(m.xauPct, 1) + "%", 306, 105, 11, m.xauPct >= 0 ? C_GREEN : C_RED, middle_right);
  fadeH(12, 308, 134);
  if (m.setValid) {
    uint32_t col = m.setChange > 0 ? C_GREEN : m.setChange < 0 ? C_RED : C_WHITE;
    txt("SET", 14, 148, 11, C_TEAL, middle_left);
    bool open = m.setState == "open";
    const char* st = open ? "ตลาดเปิด" : "ตลาดปิด";
    int pw = tfW(TF10, st) + 14;
    pill(44, 141, pw, 15, open ? RGB(30, 150, 90) : RGB(70, 76, 100));
    tf(TF10, st, 44 + pw / 2, 152, C_WHITE, 1);
    txt(thousands(m.setPrice, 2), 14, 177, 24, C_WHITE, middle_left);
    txt(signedStr(m.setChange, 2) + "  (" + signedStr(m.setPct, 2) + "%)", 14, 199, 11, col, middle_left);
    txt("SET50  " + String(m.set50Price, 2) + "  " + signedStr(m.set50Pct, 2) + "%", 14, 217, 9, C_MUTED, middle_left);
    if (m.sparkCount >= 2) {
      float lo = m.spark[0], hi = m.spark[0];
      for (int i = 0; i < m.sparkCount; i++) { lo = min(lo, m.spark[i]); hi = max(hi, m.spark[i]); }
      if (!isnan(m.setPrev)) { lo = min(lo, m.setPrev); hi = max(hi, m.setPrev); }
      if (hi - lo < 0.5f) { hi += 0.25f; lo -= 0.25f; }
      const int x0 = 160, x1 = 306, y0 = 152, y1 = 222;
      auto X = [&](int i) { return x0 + (float)i * (x1 - x0) / (m.sparkCount - 1); };
      auto Y = [&](float v) { return y1 - (v - lo) / (hi - lo) * (y1 - y0); };
      for (int i = 0; i + 1 < m.sparkCount; i++)
        for (int xx = (int)X(i); xx < (int)X(i + 1); xx++) {
          float t = (xx - X(i)) / (X(i + 1) - X(i)), yy = Y(m.spark[i] + (m.spark[i + 1] - m.spark[i]) * t);
          for (int y = (int)yy; y < y1; y++) blendPx(xx, y, col, (uint8_t)(70 - (y - yy) * 60 / max(1.0f, y1 - yy)));
        }
      if (!isnan(m.setPrev))
        for (int x = x0; x < x1; x += 6) g->drawFastHLine(x, (int)Y(m.setPrev), 3, RGB(140, 150, 180));
      for (int i = 1; i < m.sparkCount; i++) g->drawWideLine(X(i - 1), Y(m.spark[i - 1]), X(i), Y(m.spark[i]), 1.3f, col);
      int lx = X(m.sparkCount - 1), ly = Y(m.spark[m.sparkCount - 1]);
      glow(lx, ly, 8, col, 120);
      g->fillSmoothCircle(lx, ly, 3, col);
    }
  }
  endFrame();
}

// ---------------- หน้า 9: ดวงอาทิตย์ · ดวงจันทร์ ----------------
int hmToMin(const String& s) { return s.length() >= 5 ? s.substring(0, 2).toInt() * 60 + s.substring(3, 5).toInt() : -1; }
void drawSky() {
  beginFrame(BG_PAGE);
  headerBar("ดวงอาทิตย์ · จันทร์");
  const int cx = 160, cy = 131, rx = 128, ry = 92;
  fadeH(8, 312, cy, RGB(255, 255, 255), 70);
  int rise = Wx ? hmToMin(Wx->sunrise) : -1, set = Wx ? hmToMin(Wx->sunset) : -1, nm = nowMinute();
  if (rise >= 0 && set > rise) {
    float f = nm < rise ? 0 : nm > set ? 1 : (float)(nm - rise) / (set - rise);
    for (int k = 0; k < 120; k++) {
      float t = k / 120.0f, th = PI + t * PI;
      bool past = t <= f;
      g->fillSmoothCircle(cx + rx * cosf(th), cy + ry * sinf(th), past ? 1.3f : 0.7f, past ? C_GOLD : RGB(110, 100, 90));
    }
    txt(Wx->sunrise, 32, 143, 11, C_GOLD, middle_center);
    txt(Wx->sunset, 288, 143, 11, C_VIOLET, middle_center);
    int len = set - rise;
    tf(TF10, "กลางวันยาว", cx, 108, C_MUTED, 1);
    tf(TF13B, String(len / 60) + " ชม. " + String(len % 60) + " นาที", cx, 124, C_WHITE, 1);
    if (nm >= rise && nm <= set) {
      float th = PI + f * PI;
      int sx = cx + rx * cosf(th), sy = cy + ry * sinf(th);
      float pl = 0.5f + 0.5f * sinf(millis() / 400.0f);
      glow(sx, sy, 24, RGB(255, 170, 40), 110 + 40 * pl);
      g->fillSmoothCircle(sx, sy, 10, RGB(255, 200, 80));
      g->fillSmoothCircle(sx, sy, 7, RGB(255, 240, 180));
    } else {
      tf(TF13B, "กลางคืน", cx, 80, C_VIOLET, 1);
    }
  }
  fadeH(12, 308, 156);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    Lunar L = lunarOf(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    // ดวงจันทร์ใหญ่ ตามวันขึ้น/แรม
    float ph = L.ok ? (L.day - 1 + 0.5f) / L.len : 0;
    const int mx = 48, my = 198, mr = 28;
    glow(mx, my, mr + 12, RGB(220, 220, 255), 50);
    g->fillSmoothCircle(mx, my, mr, RGB(52, 56, 80));
    float c = cosf(2 * PI * ph);
    for (int y = -mr; y <= mr; y++) {
      float hw = sqrtf((float)(mr * mr - y * y)), a0, a1;
      if (ph < 0.5f) { a0 = hw * c; a1 = hw; } else { a0 = -hw; a1 = -hw * c; }
      if (a1 > a0) g->drawFastHLine(mx + (int)roundf(a0), my + y, max(1, (int)roundf(a1 - a0)), RGB(245, 238, 210));
    }
    if (L.ok) {
      tf(TF13B, lunarText(L), 90, 183, C_WHITE);
      static const char* const names[] = {"เดือนดับ", "ข้างขึ้น เสี้ยว", "ข้างขึ้น ครึ่งดวง", "ข้างขึ้น ค่อนดวง", "จันทร์เต็มดวง",
                                          "ข้างแรม ค่อนดวง", "ข้างแรม ครึ่งดวง", "ข้างแรม เสี้ยว"};
      int lit = (int)lroundf((1 - c) / 2 * 100);
      tf(TF10, String(names[(int)floorf(ph * 8 + 0.5f) % 8]) + " · สว่าง " + lit + "%", 90, 203, C_MUTED);
      // วันพระถัดไป
      for (int k = 0; k < 16; k++) {
        time_t tt = time(nullptr) + k * 86400L;
        struct tm d;
        localtime_r(&tt, &d);
        Lunar n = lunarOf(d.tm_year + 1900, d.tm_mon + 1, d.tm_mday);
        if (!n.ok || !isHoly(n)) continue;
        String when = k == 0 ? String("วันนี้") : k == 1 ? String("พรุ่งนี้ ") + WD_SHORT[d.tm_wday] + " " + d.tm_mday + " " + MON_SHORT[d.tm_mon]
                                               : String(WD_SHORT[d.tm_wday]) + " " + d.tm_mday + " " + MON_SHORT[d.tm_mon];
        String lun = n.day <= 15 ? "ขึ้น " + String(n.day) : "แรม " + String(n.day - 15);
        tf(TF10, "วันพระถัดไป: " + when + " (" + lun + " ค่ำ)", 90, 221, C_GOLD);
        break;
      }
    }
  }
  endFrame();
}

// ---------------- หน้า 5: อากาศตอนนี้ ----------------
void drawNow() {
  beginFrame(BG_PAGE);
  headerBar("อากาศตอนนี้");
  if (!Wx || !Wx->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  glow(70, 76, 56, w.isDay ? RGB(255, 190, 80) : RGB(140, 150, 255), 60);
  icon(ICONS_L, iconFor(w.weatherCode, w.isDay), 8, 28);
  int x = drawGlyphs(BIG_GLYPHS, f0(w.temp).c_str(), 138, 40);
  txt("°C", x + 2, 52, 18, C_GOLD, middle_left);
  tf(TF13B, WeatherApi::describe(w.weatherCode), 140, 108, C_WHITE);
  int fx = 140 + tf(TF10, "รู้สึกเหมือน", 140, 126, C_MUTED) + 6;
  txt(f0(w.feelsLike) + "°", fx, 122, 11, C_FEEL, middle_left);
  struct Gauge { String v; const char* lab; float f; uint32_t c1, c2; } gs[4];
  gs[0] = {w.humidity < 0 ? String("-") : String(w.humidity) + "%", "ความชื้น", w.humidity / 100.0f, RGB(90, 200, 255), RGB(0, 140, 255)};
  gs[1] = {f0(w.windSpeed), "ลม กม./ชม.", w.windSpeed / 40.0f, RGB(120, 255, 190), RGB(0, 200, 140)};
  if (A && A->valid) gs[2] = {f0(A->pm25), "PM2.5", A->pm25 / 75.0f, RGB(130, 240, 160), pmColor(A->pm25)};
  else gs[2] = {"-", "PM2.5", 0, C_MUTED, C_MUTED};
  float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
  gs[3] = {f0(uv), "UV", uv / 11.0f, RGB(255, 170, 90), uvColor(uv)};
  for (int i = 0; i < 4; i++) {
    int cx = 42 + i * 78;
    ring(cx, 172, 24, 5, isnan(gs[i].f) ? 0 : gs[i].f, gs[i].c1, gs[i].c2);
    txt(gs[i].v, cx, 172, 14, C_WHITE, middle_center);
    tf(TF10, gs[i].lab, cx, 215, C_MUTED, 1);
  }
  endFrame();
}

// ---------------- หน้า 6: พยากรณ์ ----------------
void drawForecast() {
  beginFrame(BG_PAGE);
  headerBar("พยากรณ์อากาศ");
  if (!Wx || !Wx->valid || Wx->hCount < 2) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  int n = min(w.hCount, 13);
  const int x0 = 16, x1 = 304, y0 = 38, y1 = 92, yb = 112;
  float lo = 99, hi = -99;
  for (int i = 0; i < n; i++) if (!isnan(w.hTemp[i])) { lo = min(lo, w.hTemp[i]); hi = max(hi, w.hTemp[i]); }
  if (hi - lo < 3) { hi += 1.5f; lo -= 1.5f; }
  auto X = [&](int i) { return x0 + (float)i * (x1 - x0) / (n - 1); };
  auto Y = [&](float v) { return y0 + (hi - v) / (hi - lo) * (y1 - y0); };
  // พื้นที่ใต้กราฟไล่สีทอง
  for (int px = x0; px <= x1; px++) {
    float fi = (px - x0) * (float)(n - 1) / (x1 - x0);
    int i = min((int)fi, n - 2);
    float yy = Y(w.hTemp[i] + (w.hTemp[i + 1] - w.hTemp[i]) * (fi - i));
    for (int py = (int)yy; py < yb; py++) blendPx(px, py, RGB(255, 190, 90), (uint8_t)(95 - (py - yy) * 85 / max(1.0f, yb - yy)));
  }
  for (int i = 1; i < n; i++) g->drawWideLine(X(i - 1), Y(w.hTemp[i - 1]), X(i), Y(w.hTemp[i]), 1.4f, C_GOLD);
  for (int i = 0; i < n; i++) {
    int bx = X(i);
    int h = max(0, w.hRain[i]) * 18 / 100;
    if (h > 0) g->fillSmoothRoundRect(bx - 3, yb - h, 6, h, 2, w.hRain[i] >= 60 ? RGB(90, 180, 255) : RGB(50, 90, 140));
    if (i % 3 == 0) {
      glow(bx, Y(w.hTemp[i]), 6, C_GOLD, 120);
      g->fillSmoothCircle(bx, Y(w.hTemp[i]), 3, C_GOLD);
      txt(f0(w.hTemp[i]) + "°", bx, Y(w.hTemp[i]) - 9, 9, C_WHITE, middle_center);
      txt(hh(w.hHour[i]), bx, 121, 9, C_MUTED, middle_center);
    }
  }
  // รายวัน: วันนี้ + 3 วัน (ไม่มีกรอบ เส้นแบ่งจาง)
  struct Row { int code; float lo, hi; int rain; String date; } rows[4];
  rows[0] = {w.weatherCode, w.tempMin, w.tempMax, w.rainChance, ""};
  for (int i = 0; i < 3; i++) rows[i + 1] = {w.days[i].code, w.days[i].tmin, w.days[i].tmax, w.days[i].rainChance, w.days[i].date};
  float gmin = 99, gmax = -99;
  for (auto& r : rows) { if (!isnan(r.lo)) gmin = min(gmin, r.lo); if (!isnan(r.hi)) gmax = max(gmax, r.hi); }
  if (gmax - gmin < 1) gmax = gmin + 1;
  for (int i = 0; i < 4; i++) {
    int y = 144 + i * 22;
    if (i) fadeH(14, 306, y - 11);
    String name = i == 0 ? String("วันนี้") : i == 1 ? String("พรุ่งนี้") : String("-");
    if (i >= 2) { int wd = WeatherApi::weekday(rows[i].date); if (wd >= 0) name = WD_FULL[wd]; }
    tf(TF13B, name, 16, y + 5, i == 0 ? C_GOLD : C_WHITE);
    icon(ICONS_S, iconFor(rows[i].code, true), 82, y - 12);
    txt(rows[i].rain < 0 ? String("-") : String(rows[i].rain) + "%", 140, y, 9, C_RAIN, middle_left);
    if (!isnan(rows[i].lo) && !isnan(rows[i].hi)) {
      for (int k = 0; k < 4; k++) g->drawFastHLine(196, y - 2 + k, 80, RGB(44, 48, 76));
      int bx0 = 196 + (int)((rows[i].lo - gmin) / (gmax - gmin) * 76);
      int bx1 = 196 + (int)((rows[i].hi - gmin) / (gmax - gmin) * 76);
      if (bx1 - bx0 < 6) bx1 = bx0 + 6;
      for (int k = 0; k < 4; k++) g->drawGradientHLine(bx0, y - 2 + k, bx1 - bx0, RGB(90, 200, 255), RGB(255, 170, 90));
      txt(f0(rows[i].lo) + "°", 190, y, 9, C_MUTED, middle_right);
      txt(f0(rows[i].hi) + "°", 306, y, 11, C_WHITE, middle_right);
    }
  }
  endFrame();
}

void render() {
  screen = SCR_WEATHER;
  g_aux = page + 1;
  if (page == PAGE_HOME) drawHome();
  else if (page == PAGE_SUM) drawSummary();
  else if (page == PAGE_RADAR) drawRadar();
  else if (page == PAGE_THRADAR) drawMapPage(0);
  else if (page == PAGE_STORM) drawMapPage(1);
  else if (page == PAGE_RAIN) drawRain();
  else if (page == PAGE_NOW) drawNow();
  else if (page == PAGE_FC) drawForecast();
  else if (page == PAGE_AIR) drawAir();
  else if (page == PAGE_MARKET) drawMarket();
  else drawSky();
}

void message(ThaiId id, const String& sub, uint32_t subCol) {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  thai(id, 160, 100, C_WHITE, 1);
  if (sub.length()) txt(sub, 160, 140, 18, subCol, middle_center);
  endFrame();
}


// ================== ปรับทัช + ตั้งค่า Wi-Fi บนจอ ==================
struct Cal { float ax, bx, ay, by; uint8_t swap; uint8_t ok; } cal = {1, 0, 1, 0, 0, 0};
void (*idleHook)() = nullptr;
Preferences uiPrefs;

void loadCal() {
  uiPrefs.begin("s3ui", true);
  if (uiPrefs.getBytesLength("cal") == sizeof(Cal)) uiPrefs.getBytes("cal", &cal, sizeof(Cal));
  uiPrefs.end();
}
void saveCal() {
  uiPrefs.begin("s3ui", false);
  uiPrefs.putBytes("cal", &cal, sizeof(Cal));
  uiPrefs.end();
}
// อ่านจุดที่แตะ (พิกัดดิบ)
bool rawTouch(int& rx, int& ry) {
  if (touchKind == TOUCH_NONE) return false;
  lgfx::touch_point_t tp;
  if (gfx->getTouchRaw(&tp, 1) == 0) return false;
  rx = tp.x;
  ry = tp.y;
  return true;
}
// อ่านจุดที่แตะ (พิกัดบนจอ หลังปรับทัชแล้ว)
bool screenTouch(int& x, int& y) {
  int rx, ry;
  if (!rawTouch(rx, ry)) return false;
  float a = cal.swap ? ry : rx, b = cal.swap ? rx : ry;
  x = constrain((int)(cal.ax * a + cal.bx), 0, 319);
  y = constrain((int)(cal.ay * b + cal.by), 0, 239);
  return true;
}
// รอให้แตะแล้วปล่อย คืนค่าเฉลี่ยของพิกัดดิบ
void waitTapRaw(int& rx, int& ry) {
  while (true) {
    int x, y;
    while (!rawTouch(x, y)) { if (idleHook) idleHook(); delay(10); }
    long sx = 0, sy = 0;
    int n = 0;
    unsigned long t0 = millis();
    while (rawTouch(x, y)) { sx += x; sy += y; n++; delay(10); }
    if (n >= 2 && millis() - t0 > 40) { rx = sx / n; ry = sy / n; delay(150); return; }
  }
}
void calTarget(int x, int y) {
  beginFrame(BG_PLAIN);
  thai(TH_CAL_TITLE, 160, 100, C_GOLD, 1);
  thai(TH_CAL_TAP, 160, 128, C_WHITE, 1);
  g->drawCircle(x, y, 12, C_GOLD);
  g->fillSmoothCircle(x, y, 5, C_GOLD);
  g->drawFastHLine(x - 18, y, 36, C_GOLD);
  g->drawFastVLine(x, y - 18, 36, C_GOLD);
  endFrame();
}
void runCalibration() {
  screen = SCR_SETUP;
  int r1x, r1y, r2x, r2y;
  calTarget(24, 24);
  waitTapRaw(r1x, r1y);
  calTarget(296, 216);
  waitTapRaw(r2x, r2y);
  bool swap = abs(r2y - r1y) > abs(r2x - r1x);  // แกนดิบไหนเปลี่ยนตามแนวนอนมากกว่า
  float ax0 = swap ? r1y : r1x, ax1 = swap ? r2y : r2x;
  float ay0 = swap ? r1x : r1y, ay1 = swap ? r2x : r2y;
  if (fabs(ax1 - ax0) < 20 || fabs(ay1 - ay0) < 20) { runCalibration(); return; }
  cal.swap = swap;
  cal.ax = (296 - 24) / (ax1 - ax0);
  cal.bx = 24 - cal.ax * ax0;
  cal.ay = (216 - 24) / (ay1 - ay0);
  cal.by = 24 - cal.ay * ay0;
  cal.ok = 1;
  saveCal();
  Serial.printf("[Touch] ปรับทัชแล้ว swap=%d ax=%.3f ay=%.3f\n", cal.swap, cal.ax, cal.ay);
}

// ----- รายการ Wi-Fi -----
struct Net { String ssid; int rssi; bool secure; };
Net nets[20];
int netCount = 0, netTop = 0;
String apName, apAddr, chosen, typed;
enum SetupStage { ST_LIST, ST_KEYS } stage = ST_LIST;
int kbLayer = 0;  // 0 ตัวเล็ก 1 ตัวใหญ่ 2 สัญลักษณ์
bool setupActive = false;
unsigned long lastSetupTouch = 0;

void scanNets() {
  beginFrame(BG_PLAIN);
  thai(TH_WF_SCANNING, 160, 110, C_WHITE, 1);
  endFrame();
  int n = WiFi.scanNetworks();
  netCount = 0;
  for (int i = 0; i < n && netCount < 20; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < netCount; j++) if (nets[j].ssid == s) { dup = true; break; }
    if (dup) continue;
    nets[netCount++] = {s, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
  }
  WiFi.scanDelete();
  // เรียงจากสัญญาณแรงไปอ่อน
  for (int i = 0; i < netCount; i++)
    for (int j = i + 1; j < netCount; j++)
      if (nets[j].rssi > nets[i].rssi) std::swap(nets[i], nets[j]);
  netTop = 0;
}

void button(int x, int y, int w, int h, ThaiId label, uint32_t fill, uint32_t textCol) {
  g->fillSmoothRoundRect(x, y, w, h, 8, fill);
  thai(label, x + w / 2, y + h / 2, textCol, 1);
}
void drawList() {
  beginFrame(BG_PLAIN);
  thai(TH_WF_TITLE, 12, 14, C_GOLD);
  if (netCount == 0) thai(TH_WF_NONE, 160, 110, C_MUTED, 1);
  for (int i = 0; i < 5 && netTop + i < netCount; i++) {
    const Net& n = nets[netTop + i];
    int y = 30 + i * 35;
    g->fillSmoothRoundRect(10, y, 300, 31, 8, RGB(26, 32, 48));
    g->drawRoundRect(10, y, 300, 31, 8, RGB(90, 80, 60));
    String s = n.ssid.length() > 24 ? n.ssid.substring(0, 23) + "." : n.ssid;
    txt(s, 22, y + 16, 14, C_WHITE, middle_left);
    int bars = n.rssi > -60 ? 3 : n.rssi > -72 ? 2 : 1;
    for (int b = 0; b < 3; b++) g->fillRect(284 + b * 6, y + 21 - b * 4, 4, 4 + b * 4, b < bars ? C_TEAL : RGB(70, 80, 100));
    if (n.secure) { g->drawRoundRect(266, y + 13, 9, 8, 2, C_MUTED); g->drawCircle(270, y + 12, 3, C_MUTED); }
  }
  button(10, 206, 100, 30, TH_WF_RESCAN, RGB(60, 70, 96), C_WHITE);
  if (netTop > 0) { g->fillSmoothRoundRect(118, 206, 44, 30, 8, RGB(60, 70, 96)); g->fillTriangle(140, 213, 132, 228, 148, 228, C_WHITE); }
  if (netTop + 5 < netCount) { g->fillSmoothRoundRect(168, 206, 44, 30, 8, RGB(60, 70, 96)); g->fillTriangle(190, 229, 182, 214, 198, 214, C_WHITE); }
  thai(TH_WF_PHONE, 218, 214, C_MUTED);
  txt(apName, 218, 229, 9, C_GOLD, middle_left);
  endFrame();
}

const char* KB_ROWS[3][4] = {
  {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"},
  {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
  {"1234567890", "!@#$%^&*()", "-_=+[]{};:", "'\",.?/<>"},
};
struct Key { int x, y, w, h; char c; int special; };  // special: 0 ตัวอักษร 1 shift 2 ลบ 3 สลับ 4 เว้นวรรค 5 ย้อนกลับ 6 เชื่อมต่อ
Key keys[60];
int keyCount = 0;

void layoutKeys() {
  keyCount = 0;
  const int top = 60, kh = 33, gap = 3;
  for (int r = 0; r < 4; r++) {
    const char* row = KB_ROWS[kbLayer][r];
    int n = strlen(row);
    int kw = 29;
    if (r == 3) kw = min(29, (228 + gap) / n - gap);  // แถวล่างเว้นที่ให้ปุ่ม shift / ลบ
    int rowW = n * (kw + gap) - gap;
    int x = r == 3 ? 46 + (228 - rowW) / 2 : (320 - rowW) / 2;
    for (int i = 0; i < n; i++) keys[keyCount++] = {x + i * (kw + gap), top + r * (kh + gap), kw, kh, row[i], 0};
  }
  int y3 = top + 3 * (kh + gap);
  keys[keyCount++] = {4, y3, 40, kh, 0, 1};
  keys[keyCount++] = {276, y3, 40, kh, 0, 2};
  int y4 = top + 4 * (kh + gap);
  keys[keyCount++] = {4, y4, 54, 32, 0, 3};
  keys[keyCount++] = {62, y4, 96, 32, ' ', 4};
  keys[keyCount++] = {162, y4, 66, 32, 0, 5};
  keys[keyCount++] = {232, y4, 84, 32, 0, 6};
}
void drawKeys(int pressed = -1) {
  beginFrame(BG_PLAIN);
  txt(chosen, 12, 10, 11, C_GOLD, middle_left);
  thai(TH_WF_PASS, 308, 10, C_MUTED, 2);
  g->fillSmoothRoundRect(8, 20, 304, 34, 8, RGB(20, 24, 36));
  g->drawRoundRect(8, 20, 304, 34, 8, C_GOLD);
  String shown = typed;
  useFont(18);
  while (shown.length() > 1 && g->textWidth(shown) > 284) shown.remove(0, 1);
  txt(shown, 16, 37, 18, C_WHITE, middle_left);
  int cx = 16 + txtW(shown, 18) + 2;
  if ((millis() / 500) % 2) g->fillRect(cx, 27, 2, 20, C_GOLD);
  for (int i = 0; i < keyCount; i++) {
    const Key& k = keys[i];
    uint32_t fill = i == pressed ? C_GOLD : (k.special == 6 ? RGB(46, 150, 90) : k.special ? RGB(60, 70, 96) : RGB(40, 46, 64));
    g->fillSmoothRoundRect(k.x, k.y, k.w, k.h, 6, fill);
    uint32_t tc = i == pressed ? RGB(20, 16, 10) : C_WHITE;
    if (k.special == 0) txt(String(k.c), k.x + k.w / 2, k.y + k.h / 2, 14, tc, middle_center);
    else if (k.special == 1) txt(kbLayer == 2 ? "abc" : kbLayer == 1 ? "ABC" : "abc", k.x + k.w / 2, k.y + k.h / 2, 11, kbLayer == 1 ? C_GOLD : tc, middle_center);
    else if (k.special == 2) txt("<-", k.x + k.w / 2, k.y + k.h / 2, 14, tc, middle_center);
    else if (k.special == 3) txt(kbLayer == 2 ? "abc" : "?123", k.x + k.w / 2, k.y + k.h / 2, 11, tc, middle_center);
    else if (k.special == 4) txt("space", k.x + k.w / 2, k.y + k.h / 2, 11, C_MUTED, middle_center);
    else if (k.special == 5) thai(TH_WF_CANCEL, k.x + k.w / 2, k.y + k.h / 2, tc, 1);
    else if (k.special == 6) thai(TH_WF_CONNECT, k.x + k.w / 2, k.y + k.h / 2, tc, 1);
  }
  endFrame();
}
int hitKey(int x, int y) {
  for (int i = 0; i < keyCount; i++)
    if (x >= keys[i].x - 1 && x <= keys[i].x + keys[i].w + 1 && y >= keys[i].y - 1 && y <= keys[i].y + keys[i].h + 1) return i;
  return -1;
}
// คืนพิกัดเมื่อ "ปล่อยนิ้ว" (แตะ 1 ครั้ง)
bool tapAt(int& x, int& y) {
  static bool down = false;
  static int lx = 0, ly = 0;
  int tx, ty;
  if (screenTouch(tx, ty)) { down = true; lx = tx; ly = ty; lastSetupTouch = millis(); return false; }
  if (down) { down = false; x = lx; y = ly; return true; }
  return false;
}


// ---------- โหมดภาพ (แก้จอซีด/ไม่สว่าง) ----------
bool waitTap(unsigned long ms) {
  unsigned long t0 = millis();
  int x, y;
  while (rawTouch(x, y)) { if (idleHook) idleHook(); delay(10); if (millis() - t0 > ms) return false; }  // รอปล่อยนิ้วก่อน
  while (millis() - t0 < ms) {
    if (rawTouch(x, y)) {
      unsigned long td = millis();
      while (rawTouch(x, y) && millis() - td < 3000) delay(10);
      if (millis() - td > 30) return true;
    }
    if (idleHook) idleHook();
    delay(10);
  }
  return false;
}

// ส่งชุดคำสั่งเริ่มจอใหม่ตรง ๆ (ไม่ init บัส/ไฟจอซ้ำ) แล้วตั้งสี/ทิศ/กลับสีเหมือนเดิม
void applyPreset(uint8_t p) {
  panelPreset = p % PRESET_COUNT;
  const uint8_t* l = PRESETS[panelPreset];
  gfx->startWrite();
  for (;;) {
    uint8_t cmd = *l++, n = *l++;
    if (cmd == 0xFF && n == 0xFF) break;
    bool wait = n & CMD_DELAY;
    n &= ~CMD_DELAY;
    gfx->writeCommand(cmd);
    for (int i = 0; i < n; i++) gfx->writeData(*l++);
    if (wait) {
      uint16_t ms = *l++;
      gfx->endWrite();
      delay(ms == 255 ? 500 : ms);
      gfx->startWrite();
    }
  }
  gfx->endWrite();
  gfx->setColorDepth(16);     // COLMOD + MADCTL (ทิศจอ)
  gfx->setRotation(gfx->getRotation());  // ย้ำทิศจอ (กันภาพกลับหัวหลังรีเซ็ตจอ)
  gfx->invertDisplay(false);  // INVON ตามค่าตั้งของจอ
  gfx->setBrightness(BRIGHTNESS_DAY);
  brightNow = BRIGHTNESS_DAY;
}

void drawPresetTest(uint8_t p, int secs, bool vividStep = false) {
  beginFrame(BG_PLAIN);
  ThaiId title = vividStep ? TH_VIVID_NAME : TH_PRESET_NAME;
  thai(title, 14, 18, C_GOLD);
  txt(vividStep ? String(vividLevel) + "/3" : String(p + 1) + "/" + PRESET_COUNT, 14 + thaiW(title) + 8, 18, 18, C_WHITE, middle_left);
  txt(String(secs), 306, 18, 18, C_MUTED, middle_right);
  // แถบสีทดสอบ
  static const uint32_t bars[] = {0xFFFFFF, 0xFFD678, 0xFF3C3C, 0x3CFF6E, 0x288CFF, 0x00E6FF, 0xB464FF, 0xFF64DC};
  for (int i = 0; i < 8; i++) g->fillSmoothRoundRect(14 + i * 37, 36, 33, 46, 6, bars[i]);
  for (int x = 0; x < 292; x++) {
    uint8_t v = x * 255 / 291;
    g->drawFastVLine(14 + x, 88, 14, g->color888(v, v, v));
  }
  thai(TH_PRESET_Q, 160, 130, C_WHITE, 1);
  thai(TH_PRESET_TAP, 160, 164, C_TEAL, 1);
  thai(TH_PRESET_WAIT, 160, 196, C_MUTED, 1);
  endFrame();
}

void saveVivid() {
  uiPrefs.begin("s3ui", false);
  uiPrefs.putUChar("vivid", vividLevel);
  uiPrefs.end();
}
// ขั้นที่ 2: เลือกความสดของสี (สดใส → จัดเต็ม → เล็กน้อย → ปิด) ไม่แตะ = สดใส
void chooseVivid() {
  const uint8_t order[] = {2, 3, 1, 0};
  for (int round = 0; round < 2; round++) {
    for (uint8_t lv : order) {
      vividLevel = lv;
      buildVivid();
      for (int secs = 5; secs > 0; secs--) {
        drawPresetTest(panelPreset, secs, true);
        if (waitTap(1000)) { saveVivid(); return; }
      }
    }
  }
  vividLevel = VIVID_DEFAULT;
  buildVivid();
  saveVivid();
}

uint8_t presetTries = 0;
bool presetDone = false;

void savePreset(uint8_t p, bool done) {
  uiPrefs.begin("s3ui", false);
  uiPrefs.putUChar("preset", p);
  uiPrefs.putBool("pdone", done);
  uiPrefs.putUChar("ptries", presetTries);
  uiPrefs.end();
}

}  // namespace (ปิดชั่วคราวเพื่อประกาศฟังก์ชันสาธารณะด้านล่าง)
namespace {

void applyBrightness() {
  uint8_t target = BRIGHTNESS_DAY;
  struct tm t;
  if (getLocalTime(&t, 0) && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR) && millis() > wakeUntil)
    target = BRIGHTNESS_NIGHT;
  if (target != brightNow) {
    brightNow = target;
    gfx->setBrightness(target);
  }
}

}  // namespace

namespace Display {

const char* modeName(uint8_t mode) {
  static const char* n[] = {"หมุน 90°", "หมุน 270°", "หมุน 90° (กลับซ้าย-ขวา)", "หมุน 270° (กลับซ้าย-ขวา)"};
  return n[mode % MODE_COUNT];
}

void begin(uint8_t mode) {
  detectTouch();
  loadCal();
  uiPrefs.begin("s3ui", true);
  panelPreset = uiPrefs.getUChar("preset", 0) % PRESET_COUNT;
  uiPrefs.end();
  uiPrefs.begin("s3ui", true);
  vividLevel = uiPrefs.getUChar("vivid", VIVID_DEFAULT) % 4;
  uiPrefs.end();
  unsigned long t0 = millis();
  buildVivid();
  Serial.printf("[Display] โหมดภาพ %u, เร่งสี %u (%lu ms)\n", panelPreset + 1, vividLevel, millis() - t0);
  gfx = new LGFX(touchKind, touchSda, touchScl, touchAddr);
  gfx->init();
  static const uint8_t rot[] = {5, 7, 1, 3};
  gfx->setRotation(rot[mode % MODE_COUNT]);
  gfx->setBrightness(BRIGHTNESS_DAY);
  brightNow = BRIGHTNESS_DAY;
  canvas = new LGFX_Sprite(gfx);
  canvas->setColorDepth(16);
  canvas->setPsram(true);
  if (!canvas->createSprite(320, 240)) {
    delete canvas;
    canvas = nullptr;
    Serial.println("[Display] ไม่มี PSRAM พอสำหรับเฟรม — วาดตรงลงจอ");
  } else {
    bgCache = new LGFX_Sprite(gfx);
    bgCache->setColorDepth(16);
    bgCache->setPsram(true);
    if (!bgCache->createSprite(320, 240)) {
      delete bgCache;
      bgCache = nullptr;
    }
  }
  Serial.printf("[Display] พร้อม (PSRAM %u KB, เฟรม %s)\n", ESP.getPsramSize() / 1024, canvas ? "sprite" : "direct");
}

void setRgb(bool, bool, bool) {}
void beep() {}

void showBoot(const char* version) {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  txt(APP_NAME, 160, 100, 24, C_GOLD, middle_center);
  txt(String("v") + version, 160, 132, 11, C_MUTED, middle_center);
  endFrame();
}
void showBootReason(const String& info, uint32_t streak, bool safe) {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  txt(APP_NAME, 160, 60, 24, C_GOLD, middle_center);
  txt(String("v") + APP_VERSION, 160, 88, 11, C_MUTED, middle_center);
  tf(TF13B, "บอร์ดรีสตาร์ทเพราะ:", 160, 122, C_WHITE, 1);
  txt(info, 160, 142, 14, C_RED, middle_center);
  txt(String("x") + streak + (safe ? "  safe mode" : ""), 160, 164, 11, C_MUTED, middle_center);
  tf(TF12, "ถ่ายรูปหน้านี้ส่งให้ผู้พัฒนา · แตะจอเพื่อไปต่อ", 160, 196, C_GOLD, 1);
  endFrame();
}
void showConnecting(const String& ssid) { message(TH_CONNECTING, ssid, C_GOLD); }
void showWifiFailed() { message(TH_WIFI_FAILED, "", C_WHITE); }
void showLoading() { message(TH_LOADING, "", C_WHITE); }

void showPortal(const char* apSsid, const String& ip) {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  thai(TH_SETUP_TITLE, 160, 26, C_GOLD, 1);
  thai(TH_SETUP_A, 16, 70, C_WHITE);
  thai(TH_SETUP_B, 16, 104, C_WHITE);
  txt(apSsid, 160, 134, 18, C_GOLD, middle_center);
  thai(TH_SETUP_C, 16, 170, C_WHITE);
  txt("http://" + ip, 16 + thaiW(TH_SETUP_C) + 8, 170, 14, C_TEAL, middle_left);
  endFrame();
}


void showWifiFailedReason(int st) {
  ThaiId id = st == WL_NO_SSID_AVAIL ? TH_WF_ERR_NOSSID : st == WL_CONNECT_FAILED ? TH_WF_ERR_PASS : TH_WF_ERR_OTHER;
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  thai(TH_WIFI_FAILED, 160, 96, C_RED, 1);
  thai(id, 160, 130, C_WHITE, 1);
  txt(String("code ") + st, 160, 160, 9, C_MUTED, middle_center);
  endFrame();
}

void beginWifiSetup(const char* apSsid, const String& apIp) {
  apName = apSsid;
  apAddr = apIp;
  setupActive = true;
  screen = SCR_SETUP;
  if (touchKind == TOUCH_NONE) { showPortal(apSsid, apIp); return; }
  if (!cal.ok) runCalibration();
  stage = ST_LIST;
  scanNets();
  drawList();
}

void wifiSetupBackToKeyboard() {
  screen = SCR_SETUP;
  stage = ST_KEYS;
  layoutKeys();
  drawKeys();
}

void setIdleHook(void (*hook)()) { idleHook = hook; }

bool needsPresetChoice() {
  if (touchKind == TOUCH_NONE) return false;
  uiPrefs.begin("s3ui", true);
  presetDone = uiPrefs.getBool("pdone", false);
  presetTries = uiPrefs.getUChar("ptries", 0);
  uiPrefs.end();
  return !presetDone && presetTries < 3;
}

// ลองทีละโหมด (แบบใหม่ก่อน แบบเดิมไว้ท้าย) ผู้ใช้แตะจอเมื่อภาพสวยที่สุด
// ถ้าไม่มีใครแตะเลย จะกลับเป็นโหมดเดิม (ไม่เสี่ยงจอดับ)
void choosePreset() {
  if (touchKind == TOUCH_NONE) return;
  if (!cal.ok) runCalibration();
  Screen prev = screen;
  screen = SCR_SETUP;
  uint8_t old = panelPreset;
  presetTries++;
  savePreset(old, false);
  const uint8_t order[] = {1, 2, 3, 0};
  for (int round = 0; round < 2; round++) {
    for (uint8_t p : order) {
      applyPreset(p);
      for (int secs = 8; secs > 0; secs--) {
        drawPresetTest(p, secs);
        if (waitTap(1000)) {
          Serial.printf("[Display] ผู้ใช้เลือกโหมดภาพ %u\n", p + 1);
          savePreset(p, true);
          chooseVivid();
          screen = prev;
          if (screen == SCR_WEATHER) render();
          return;
        }
      }
    }
  }
  applyPreset(old);
  savePreset(old, false);
  Serial.println("[Display] ไม่มีการแตะ — ใช้โหมดภาพเดิม (แตะจอค้าง 5 วินาทีเพื่อเลือกใหม่)");
  screen = prev;
  if (screen == SCR_WEATHER) render();
}

void setPreset(uint8_t p) {
  applyPreset(p);
  savePreset(panelPreset, true);
  if (screen == SCR_WEATHER) render();
}
uint8_t preset() { return panelPreset; }

void setVivid(uint8_t level) {
  vividLevel = level % 4;
  buildVivid();
  uiPrefs.begin("s3ui", false);
  uiPrefs.putUChar("vivid", vividLevel);
  uiPrefs.end();
  if (screen == SCR_WEATHER) render();
}
uint8_t vivid() { return vividLevel; }

bool waitTap(unsigned long ms) { return ::waitTap(ms); }
bool hasTouch() { return touchKind != TOUCH_NONE; }

void showSoundTest(int set, int total) {
  screen = SCR_SETUP;
  beginFrame(BG_PLAIN);
  thai(TH_SND_TITLE, 160, 60, C_GOLD, 1);
  thai(TH_SND_TAP, 160, 104, C_WHITE, 1);
  thai(TH_SND_SET, 150, 146, C_MUTED, 2);
  txt(String(set) + "/" + total, 158, 146, 18, C_TEAL, middle_left);
  thai(TH_SND_SKIP, 160, 190, C_MUTED, 1);
  endFrame();
}
void showSoundOk() {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  thai(TH_SND_OK, 160, 110, C_GREEN, 1);
  endFrame();
}


bool wifiSetupBusy() { return setupActive && millis() - lastSetupTouch < 120000UL; }

bool wifiSetupLoop(String& ssid, String& pass) {
  if (screen != SCR_SETUP || touchKind == TOUCH_NONE) return false;
  int x, y;
  static unsigned long lastBlink = 0;
  if (stage == ST_KEYS && millis() - lastBlink > 500) { lastBlink = millis(); drawKeys(); }
  if (!tapAt(x, y)) return false;
  if (stage == ST_LIST) {
    if (y >= 206) {
      if (x < 112) { scanNets(); drawList(); }
      else if (x < 164 && netTop > 0) { netTop -= 5; drawList(); }
      else if (x < 214 && netTop + 5 < netCount) { netTop += 5; drawList(); }
      return false;
    }
    int idx = (y - 30) / 35;
    if (y >= 30 && idx >= 0 && idx < 5 && netTop + idx < netCount) {
      chosen = nets[netTop + idx].ssid;
      typed = "";
      kbLayer = 0;
      if (!nets[netTop + idx].secure) { ssid = chosen; pass = ""; return true; }
      wifiSetupBackToKeyboard();
    }
    return false;
  }
  int k = hitKey(x, y);
  if (k < 0) return false;
  drawKeys(k);
  delay(60);
  const Key& key = keys[k];
  switch (key.special) {
    case 0: if (typed.length() < 63) typed += key.c; if (kbLayer == 1) { kbLayer = 0; layoutKeys(); } break;
    case 1: kbLayer = kbLayer == 2 ? 0 : (kbLayer == 0 ? 1 : 0); layoutKeys(); break;
    case 2: if (typed.length()) typed.remove(typed.length() - 1); break;
    case 3: kbLayer = kbLayer == 2 ? 0 : 2; layoutKeys(); break;
    case 4: if (typed.length() < 63) typed += ' '; break;
    case 5: stage = ST_LIST; drawList(); return false;
    case 6: ssid = chosen; pass = typed; return true;
  }
  drawKeys();
  return false;
}

void showUpdatingFirmware(const String& version) {
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  thai(TH_FW_UPDATING, 160, 100, C_GOLD, 1);
  txt("v" + version, 160, 136, 18, C_WHITE, middle_center);
  thai(TH_FW_WAIT, 160, 170, C_MUTED, 1);
  endFrame();
}

void showUpdating() {
  updating = true;
  if (screen == SCR_WEATHER) render();
}

void showWeather(const Settings& s, const Weather& w, const AirQuality& a, const Market& m) {
  S = &s;
  M = &m;
  Wx = &w;
  A = &a;
  updating = false;
  render();
}

void refresh() {
  if (screen == SCR_WEATHER) render();
}

void nextPage() {
  wakeUntil = millis() + 30000;
  applyBrightness();
  if (!Wx) return;
  page = (page + 1) % PAGE_COUNT;
  render();
}

void showDisplayMode(uint8_t mode) {
  static const uint8_t rot[] = {5, 7, 1, 3};
  gfx->setRotation(rot[mode % MODE_COUNT]);
  screen = SCR_MESSAGE;
  beginFrame(BG_PLAIN);
  txt(String("Mode ") + (mode + 1) + "/" + MODE_COUNT, 160, 110, 24, C_GOLD, middle_center);
  endFrame();
}

Touch loop() {
  // หน้านาฬิกาอัปเดต 5 ครั้ง/วินาที (แถบวินาทีลื่น) หน้าอื่นทุก 1 วินาที
  unsigned long now = millis();
  // หน้านาฬิกา 5 ครั้ง/วินาที, เรดาร์ 10 ครั้ง/วินาที, หน้าอื่นวาดใหม่ทุกครั้งที่วินาทีเปลี่ยน
  static time_t lastSec = 0;
  time_t sec = time(nullptr);
  bool due = page == PAGE_HOME ? now - lastFrame >= 200
           : page == PAGE_RADAR ? now - lastFrame >= 100
           : (page == PAGE_THRADAR || page == PAGE_STORM) ? now - lastFrame >= 150
           : (sec != lastSec && now - lastFrame >= 120) || now - lastFrame >= 1000;
  if (screen == SCR_WEATHER && due) {
    lastFrame = now;
    lastSec = sec;
    render();
  }
  static unsigned long lastBright = 0;
  if (now - lastBright > 5000) {
    lastBright = now;
    applyBrightness();
  }
  if (screen == SCR_SETUP) return Touch::None;  // หน้าตั้งค่า Wi-Fi จัดการการแตะเอง
  int32_t x, y;
  bool pressed = touchKind != TOUCH_NONE && gfx->getTouch(&x, &y) > 0;
  if (pressed) {
    if (touchStart == 0) { touchStart = now; longFired = false; }
    if (!longFired && now - touchStart > 5000) { longFired = true; return Touch::LongPress; }
    return Touch::None;
  }
  if (touchStart != 0) {
    unsigned long held = now - touchStart;
    touchStart = 0;
    if (!longFired && held > 40) return Touch::Tap;
  }
  return Touch::None;
}

}  // namespace Display
