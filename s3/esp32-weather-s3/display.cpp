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
    0xB6, 2, 0x0A, 0xA2,
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
    0xB6, 3, 0x08, 0xC2, 0x27,
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
  const uint8_t* f = px == 9 ? VLW_9 : px == 11 ? VLW_11 : px == 14 ? VLW_14 : px == 18 ? VLW_18 : VLW_24;
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
ThaiId descFor(int code) {
  switch (code) {
    case 0: return TH_D_CLEAR;
    case 1: return TH_D_MAINLY_CLEAR;
    case 2: return TH_D_PARTLY;
    case 3: return TH_D_OVERCAST;
    case 45: case 48: return TH_D_FOG;
    case 51: case 53: case 55: return TH_D_DRIZZLE;
    case 56: case 57: return TH_D_FRZ_DRIZZLE;
    case 61: return TH_D_RAIN_L;
    case 63: return TH_D_RAIN_M;
    case 65: return TH_D_RAIN_H;
    case 66: case 67: return TH_D_FRZ_RAIN;
    case 71: case 73: case 75: case 77: return TH_D_SNOW;
    case 80: return TH_D_SHOWER_L;
    case 81: return TH_D_SHOWER_M;
    case 82: return TH_D_SHOWER_H;
    case 85: case 86: return TH_D_SNOW_SH;
    case 95: return TH_D_STORM;
    case 96: case 99: return TH_D_STORM_HAIL;
    default: return TH_D_UNKNOWN;
  }
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
// ระดับ 0 = ปิด, 1 = เล็กน้อย, 2 = สดใส (ค่าเริ่มต้น), 3 = จัดเต็ม
uint8_t vividLevel = VIVID_DEFAULT;
uint16_t* vividLut = nullptr;  // ตารางแปลงสี 65536 ค่า (อยู่ใน PSRAM)

void buildVivid() {
  if (vividLevel == 0) return;
  if (!vividLut) vividLut = (uint16_t*)ps_malloc(65536 * sizeof(uint16_t));
  if (!vividLut) return;
  static const float SAT[] = {1.0f, 1.2f, 1.4f, 1.55f}, GAM[] = {1.0f, 0.82f, 0.7f, 0.6f}, LIFT[] = {0, 0, 0.02f, 0.035f};
  float sat = SAT[vividLevel], gam = GAM[vividLevel], lift = LIFT[vividLevel];
  // ทำตารางต่อช่องสีก่อน (เร็วกว่า powf ทุกค่า)
  for (uint32_t v = 0; v < 65536; v++) {
    uint16_t c = (uint16_t)((v & 0xFF) << 8 | v >> 8);  // sprite เก็บแบบสลับไบต์
    float r = ((c >> 11) & 31) / 31.0f, g6 = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
    float L = 0.299f * r + 0.587f * g6 + 0.114f * b;
    float ch[3] = {L + (r - L) * sat, L + (g6 - L) * sat, L + (b - L) * sat};
    for (auto& x : ch) {
      x = constrain(x, 0.0f, 1.0f);
      x = lift + (1 - lift) * powf(x, gam);
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
void pageDots(int cx, int y) {
  for (int i = 0; i < 5; i++) {
    int x = cx - 20 + i * 10;
    if (i == page) g->fillSmoothCircle(x, y, 3, C_TEAL);
    else g->drawCircle(x, y, 2, RGB(140, 150, 180));
  }
}
void headerClock() {
  pageDots(236, 12);
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  txt(b, 308, 12, 11, C_WHITE, middle_right);
}

// ---------------- สถานะ ----------------
constexpr int PAGE_COUNT = 5;
enum { PAGE_HOME = 0, PAGE_SUM, PAGE_RADAR, PAGE_NOW, PAGE_FC };

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

// ข้อมูลแผนที่ฝนที่ใช้ได้กับเมืองที่ตั้งไว้ และยังไม่เก่าเกิน 45 นาที
const RadarData* radarHere() {
  const RadarData* d = Radar::get();
  if (!d || !d->valid || !S || !Radar::usableAt(S->lat, S->lon)) return nullptr;
  time_t now = time(nullptr);
  if (now > 1700000000 && now - (time_t)d->generated > 45 * 60) return nullptr;
  return d;
}

void drawTicker() {
  const Weather& w = *Wx;
  int code = w.weatherCode;
  bool rainingNow = code >= 51 && code != 45 && code != 48 && code < 100;
  int at = -1;
  for (int i = 0; i < w.hCount && i < 12; i++)
    if (w.hRain[i] >= 60) { at = i; break; }
  uint32_t dot = C_TEAL;
  int x = 38, y = 212;
  const RadarData* rd = radarHere();
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
  } else if (rainingNow || (rd && rd->rainingHere)) {
    dot = C_RAIN;
    thai(TH_T_RAIN_NOW, x, y, RGB(210, 230, 255));
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
void drawFreshness(int xr, int y) {
  if (!Wx || !Wx->valid || !Wx->fetchedAt) return;
  int mins = (millis() - Wx->fetchedAt) / 60000;
  int x = xr;
  if (mins < 1) {
    x -= thaiW(TH_JUST_NOW);
    thai(TH_JUST_NOW, x, y, C_MUTED);
  } else {
    x -= thaiW(TH_MIN_AGO);
    thai(TH_MIN_AGO, x, y, C_MUTED);
    String m = String(mins);
    x -= txtW(m, 9) + 3;
    txt(m, x, y, 9, C_WHITE, middle_left);
    x -= thaiW(TH_UPD) + 3;
    thai(TH_UPD, x, y, C_MUTED);
  }
  // จุดกะพริบแบบ LIVE
  float ph = (millis() % 1500) / 1500.0f;
  uint8_t a = 80 + (uint8_t)(175 * (0.5f + 0.5f * cosf(ph * 6.283f)));
  g->fillSmoothCircle(x - 8, y, 4, g->color888(0, a * 230 / 255, a));
  g->fillSmoothCircle(x - 8, y, 2, C_WHITE);
}

void drawHome() {
  beginFrame(BG_HOME);
  struct tm t;
  bool haveTime = getLocalTime(&t, 0);
  String city = S ? S->city : String("");
  int comma = city.indexOf(',');
  if (comma > 0) city = city.substring(0, comma);
  txt(city, 12, 11, 11, C_GOLD, middle_left);
  drawWifi(296, 15);
  drawFreshness(284, 11);
  char hm[6] = "--:--", sec[4] = ":--";
  if (haveTime) {
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(sec, sizeof(sec), ":%02d", t.tm_sec);
  }
  // นาฬิกาตัวใหญ่ + วินาที (ฐานตัวอักษรตรงกัน)
  int x = drawGlyphs(CLOCK_GLYPHS, hm, 10, 4);
  drawGlyphs(SEC_GLYPHS, sec, x + 2, 4 + CLOCK_ASC - SEC_ASC);
  if (haveTime) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    float frac = (t.tm_sec + tv.tv_usec / 1e6f) / 60.0f;
    int w = (int)(292 * frac);
    if (w > 2) {
      int half = w / 2;
      for (int r = 0; r < 3; r++) {
        g->drawGradientHLine(14, 90 + r, half, C_TEAL, C_VIOLET);
        g->drawGradientHLine(14 + half, 90 + r, w - half, C_VIOLET, C_GOLD);
      }
      g->fillSmoothCircle(14 + w, 91, 6, g->color888(255, 214, 120) & 0xFFFFFF);
      g->fillSmoothCircle(14 + w, 91, 3, C_WHITE);
    }
    // วันที่ในแถบไล่สีเรืองแสง
    ThaiId wd = (ThaiId)(TH_WDB_0 + t.tm_wday);
    ThaiId mon = (ThaiId)(TH_MONB_0 + t.tm_mon);
    String day = String(t.tm_mday), year = String(t.tm_year + 1900 + 543);
    int cw = 14 + thaiW(wd) + 8 + txtW(day, 18) + 6 + thaiW(mon) + 6 + txtW(year, 18) + 14;
    int cx0 = 10, cy0 = 100, ch = 26;
    g->fillSmoothRoundRect(cx0, cy0, cw, ch, 13, C_TEAL);
    for (int r = 2; r < ch - 2; r++) g->drawGradientHLine(cx0 + 8, cy0 + r, cw - 16, RGB(0, 150, 255), RGB(150, 70, 255));
    g->fillSmoothRoundRect(cx0 + cw - 16, cy0, 16, ch, 8, RGB(150, 70, 255));
    g->fillSmoothRoundRect(cx0, cy0, 16, ch, 8, RGB(0, 150, 255));
    int dx = cx0 + 14, dy = cy0 + ch / 2;
    thai(wd, dx, dy, C_WHITE);
    dx += thaiW(wd) + 8;
    txt(day, dx, dy, 18, C_WHITE, middle_left);
    dx += txtW(day, 18) + 6;
    thai(mon, dx, dy, C_WHITE);
    dx += thaiW(mon) + 6;
    txt(year, dx, dy, 18, C_WHITE, middle_left);
    // ข้างแถบวันที่: พระอาทิตย์ขึ้น/ตก
    if (Wx && Wx->valid && cw < 230) {
      txt(Wx->sunrise, 308, 106, 9, C_GOLD, middle_right);
      txt(Wx->sunset, 308, 120, 9, C_VIOLET, middle_right);
    }
  }
  if (Wx && Wx->valid) {
    const Weather& w = *Wx;
    icon(ICONS_M, iconFor(w.weatherCode, w.isDay), 12, 139);
    txt(f0(w.temp) + "°", 72, 161, 24, C_WHITE, middle_left);
    txt(f0(w.feelsLike) + "°", 150, 154, 14, C_FEEL, middle_center);
    txt(w.rainChance < 0 ? String("-") : String(w.rainChance) + "%", 190, 154, 14, C_RAIN, middle_center);
    if (A && A->valid) txt(f0(A->pm25), 230, 154, 14, pmColor(A->pm25), middle_center);
    float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
    txt(f0(uv), 270, 154, 14, uvColor(uv), middle_center);
    drawTicker();
  } else {
    thai(TH_NO_DATA, 160, 161, C_MUTED, 1);
  }
  pageDots(160, 233);
  endFrame();
}

// ---------------- หน้า 2: สรุปวันนี้ ----------------
void tileValue(int i, const String& v, uint32_t col) {
  int x = 10 + (i % 2) * 154, y = 30 + (i / 2) * 56;
  txt(v, x + 14, y + 33, 18, col, middle_left);
}
int tileSubX(int i) { return 10 + (i % 2) * 154 + 140; }
int tileSubY(int i) { return 30 + (i / 2) * 56 + 34; }

void drawSummary() {
  beginFrame(BG_SUM);
  headerClock();
  if (!Wx || !Wx->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  // 1) อุณหภูมิ ต่ำ–สูง + ชั่วโมงที่ร้อนสุด
  tileValue(0, f0(w.tempMin) + "° - " + f0(w.tempMax) + "°", C_WHITE);
  int hot = -1;
  for (int i = 0; i < w.hCount && i < 12; i++) if (hot < 0 || w.hTemp[i] > w.hTemp[hot]) hot = i;
  if (hot >= 0) {
    String h = hh(w.hHour[hot]);
    txt(h, tileSubX(0), tileSubY(0) - 8, 9, C_FEEL, middle_right);
    thai(TH_S_HOTTEST, tileSubX(0), tileSubY(0) + 6, C_MUTED, 2);
  }
  // 2) ฝน
  int r0 = -1, r1 = -1, rmax = 0;
  for (int i = 0; i < w.hCount && i < 12; i++) {
    rmax = max(rmax, w.hRain[i]);
    if (w.hRain[i] >= 50) { if (r0 < 0) r0 = i; r1 = i; }
  }
  if (r0 >= 0) {
    tileValue(1, String(rmax) + "%", C_RAIN);
    txt(hh(w.hHour[r0]) + "-" + hh(w.hHour[r1] + 1), tileSubX(1), tileSubY(1) - 8, 9, C_RAIN, middle_right);
    thai(TH_S_RAINAT, tileSubX(1), tileSubY(1) + 6, C_MUTED, 2);
  } else {
    thai(TH_S_NORAIN, 10 + 154 + 14, 30 + 33, C_GREEN);
  }
  // 3) ฝุ่น
  if (A && A->valid) {
    int lv = WeatherApi::pm25Level(A->pm25);
    tileValue(2, f0(A->pm25), pmColor(A->pm25));
    if (lv >= 0) thai((ThaiId)(TH_AQS_0 + lv), tileSubX(2), tileSubY(2), pmColor(A->pm25), 2);
  } else {
    tileValue(2, "-", C_MUTED);
  }
  // 4) UV สูงสุด + ช่วงแดดแรง
  tileValue(3, isnan(w.uvMax) ? String("-") : String(w.uvMax, 1), uvColor(w.uvMax));
  int s0 = -1, s1 = -1;
  for (int i = 0; i < w.hCount; i++) {
    if (!isnan(w.hUv[i]) && w.hUv[i] >= 6) { if (s0 < 0) s0 = i; s1 = i; }
    else if (s0 >= 0) break;
  }
  if (s0 >= 0) {
    txt(hh(w.hHour[s0]) + "-" + hh(w.hHour[s1] + 1), tileSubX(3), tileSubY(3) - 8, 9, C_UV, middle_right);
    thai(TH_S_UVHIGH, tileSubX(3), tileSubY(3) + 6, C_MUTED, 2);
  } else {
    thai(TH_S_UVOK, tileSubX(3), tileSubY(3), C_GREEN, 2);
  }
  // 5) พระอาทิตย์ขึ้น / ตก
  tileValue(4, w.sunrise, C_GOLD);
  txt(w.sunset, tileSubX(4), tileSubY(4) - 6, 14, C_VIOLET, middle_right);
  thai(TH_S_SET, tileSubX(4), tileSubY(4) + 9, C_MUTED, 2);
  // 6) ความชื้น · ลม
  tileValue(5, w.humidity < 0 ? String("-") : String(w.humidity) + "%", C_TEAL);
  txt(f0(w.windSpeed), tileSubX(5) - thaiW(TH_S_KMH) - 4, tileSubY(5), 14, C_WHITE, middle_right);
  thai(TH_S_KMH, tileSubX(5), tileSubY(5), C_MUTED, 2);
  // คำแนะนำวันนี้ (ป้ายสี)
  struct Adv { ThaiId id; uint32_t c; } adv[5];
  int n = 0;
  if (r0 >= 0 || w.rainChance >= 50) adv[n++] = {TH_ADV_UMB, RGB(0, 120, 255)};
  if (!isnan(w.uvMax) && w.uvMax >= 6) adv[n++] = {TH_ADV_SUN, RGB(220, 60, 190)};
  if (A && A->valid && WeatherApi::pm25Level(A->pm25) >= 2) adv[n++] = {TH_ADV_MASK, RGB(230, 70, 80)};
  if (WeatherApi::heatLevel(w.feelsLike) >= 2) adv[n++] = {TH_ADV_WATER, RGB(230, 120, 40)};
  if (n == 0) adv[n++] = {TH_ADV_GOOD, RGB(30, 160, 90)};
  int ax = 10;
  for (int i = 0; i < n; i++) {
    int wdt = thaiW(adv[i].id) + 18;
    if (ax + wdt > 312) break;
    g->fillSmoothRoundRect(ax, 202, wdt, 24, 12, adv[i].c);
    thai(adv[i].id, ax + wdt / 2, 214, C_WHITE, 1);
    ax += wdt + 6;
  }
  endFrame();
}


// ---------------- หน้า 3: แผนที่ฝน ----------------
constexpr int MAP_X = 8, MAP_Y = 28, MAP_W = 224, MAP_H = 204, MAP_CX = MAP_X + MAP_W / 2, MAP_CY = MAP_Y + MAP_H / 2;
LGFX_Sprite* rframes[4] = {};
uint32_t rframesGen = 0;

bool ensureRadarFrames(const RadarData* d) {
  if (!d) return false;
  if (rframesGen == d->generated && rframes[0]) return true;
  for (int i = 0; i < 4; i++) {
    if (!rframes[i]) {
      rframes[i] = new LGFX_Sprite(gfx);
      rframes[i]->setColorDepth(16);
      rframes[i]->setPsram(true);
      if (!rframes[i]->createSprite(MAP_W, MAP_H)) { delete rframes[i]; rframes[i] = nullptr; return false; }
    }
    rframes[i]->fillScreen(0);
    rframes[i]->drawJpg(d->jpg[i], d->len[i], 0, 0);
  }
  rframesGen = d->generated;
  return true;
}

// เส้นโปร่งแสง (ผสมกับภาพด้านล่าง)
void blendLine(float x0, float y0, float x1, float y1, uint32_t col, uint8_t a) {
  int n = (int)max(fabsf(x1 - x0), fabsf(y1 - y0));
  uint8_t r = col >> 16, gg = (col >> 8) & 0xFF, b = col & 0xFF;
  for (int i = 0; i <= n; i++) {
    int x = (int)roundf(x0 + (x1 - x0) * i / max(1, n)), y = (int)roundf(y0 + (y1 - y0) * i / max(1, n));
    auto c = g->readPixelRGB(x, y);
    g->drawPixel(x, y, g->color888(c.R8() + (r - c.R8()) * a / 255, c.G8() + (gg - c.G8()) * a / 255, c.B8() + (b - c.B8()) * a / 255));
  }
}

void drawRadar() {
  beginFrame(BG_RADAR);
  headerClock();
  const RadarData* d = Radar::get();
  bool here = d && d->valid && S && Radar::usableAt(S->lat, S->lon);
  if (!here || !ensureRadarFrames(d)) {
    thai(d && d->valid ? TH_R_OUTSIDE : TH_R_NODATA, MAP_CX, MAP_CY, C_MUTED, 1);
    endFrame();
    return;
  }
  unsigned long now = millis();
  // ภาพเคลื่อนไหว 4 เฟรม (เฟรมล่าสุดค้างนานกว่า)
  int phase = (now / 650) % 6;
  int fi = phase >= 3 ? 3 : phase;
  rframes[fi]->pushSprite(g, MAP_X, MAP_Y);
  // วงรัศมี 10/20/30 กม.
  float ppk = 1.0f / d->kmPerPx;
  for (int k = 1; k <= 3; k++) {
    int r = (int)(k * 10 * ppk);
    g->drawCircle(MAP_CX, MAP_CY, r, RGB(70, 120, 150));
    txt(String(k * 10), MAP_CX + (int)(r * 0.707f) + 2, MAP_CY - (int)(r * 0.707f) - 2, 9, RGB(140, 190, 220), bottom_left);
  }
  g->drawFastHLine(MAP_CX - 98, MAP_CY, 196, RGB(40, 70, 90));
  g->drawFastVLine(MAP_CX, MAP_CY - 98, 196, RGB(40, 70, 90));
  // เส้นกวาดเรดาร์ + หางเรืองแสง
  float ang = (now % 4000) / 4000.0f * 6.2831853f;
  for (int k = 14; k >= 0; k--) {
    float a = ang - k * 0.035f;
    blendLine(MAP_CX, MAP_CY, MAP_CX + 96 * sinf(a), MAP_CY - 96 * cosf(a), C_TEAL, (uint8_t)(170 - k * 11));
  }
  // ตำแหน่งเมือง (จุดเต้น)
  float pulse = (now % 1600) / 1600.0f;
  g->drawCircle(MAP_CX, MAP_CY, 4 + (int)(pulse * 12), g->color888(255 * (1 - pulse), 214 * (1 - pulse), 120 * (1 - pulse)));
  g->fillSmoothCircle(MAP_CX, MAP_CY, 4, C_GOLD);
  g->fillSmoothCircle(MAP_CX, MAP_CY, 2, C_WHITE);
  // ทิศเหนือ
  txt("N", MAP_X + 7, MAP_Y + 24, 11, C_GOLD, top_left);
  // แถบเวลาของเฟรม (ด้านบน — ด้านล่างเป็นเครดิตแผนที่)
  g->fillRectAlpha(MAP_X, MAP_Y, MAP_W, 18, 150, 0);
  for (int i = 0; i < 4; i++) {
    int cx = MAP_X + 28 + i * 56, cy = MAP_Y + 9;
    bool on = i == fi;
    if (on) g->fillSmoothRoundRect(cx - 26, cy - 7, 52, 14, 7, RGB(0, 120, 160));
    if (i == 3) thai(TH_R_NOW, cx, cy, on ? C_WHITE : C_MUTED, 1);
    else txt(String("-") + (int)((d->frames[3] - d->frames[i]) / 60) + "m", cx, cy, 9, on ? C_WHITE : C_MUTED, middle_center);
  }
  // แผงสถานะด้านขวา
  const int px = 275;
  thai(TH_R_STATUS, px, 40, C_MUTED, 1);
  bool fresh = time(nullptr) < 1700000000 || time(nullptr) - (time_t)d->generated < 45 * 60;
  if (!fresh) {
    thai(TH_R_NODATA, px, 90, C_MUTED, 1);
  } else if (d->rainingHere) {
    float bl = 0.5f + 0.5f * sinf(now / 300.0f);
    g->fillSmoothRoundRect(244, 54, 62, 46, 10, g->color888(0, 60 + 60 * bl, 160 + 60 * bl));
    thai(TH_R_HERE, px, 68, C_WHITE, 1);
    thai(TH_R_HERE2, px, 88, C_WHITE, 1);
  } else if (d->approaching && d->etaMin > 0) {
    float bl = 0.5f + 0.5f * sinf(now / 250.0f);
    g->fillSmoothRoundRect(244, 52, 62, 22, 8, g->color888(200 + 55 * bl, 90, 40));
    thai(TH_R_COMING, px, 63, C_WHITE, 1);
    thai(TH_R_ETA, px, 86, C_MUTED, 1);
    txt(String(d->etaMin), px, 108, 24, C_GOLD, middle_center);
    thai(TH_R_MIN, px, 128, C_MUTED, 1);
    if (d->bearing >= 0) thai((ThaiId)(TH_DIR_0 + ((d->bearing + 22) % 360) / 45), px, 146, C_RAIN, 1);
  } else if (!isnan(d->nearestKm) && d->nearestKm <= 40) {
    thai(TH_R_NEAR, px, 64, C_RAIN, 1);
    txt(String(d->nearestKm, 0), px - 6, 94, 24, C_WHITE, middle_center);
    thai(TH_R_KM, px + 14 + txtW(String(d->nearestKm, 0), 24) / 2, 98, C_MUTED, 1);
    if (d->bearing >= 0) thai((ThaiId)(TH_DIR_0 + ((d->bearing + 22) % 360) / 45), px, 124, C_RAIN, 1);
  } else {
    thai(TH_R_NONE, px, 70, C_GREEN, 1);
    thai(TH_R_NONE2, px, 92, C_MUTED, 1);
  }
  thai(d->model ? TH_R_MODEL : TH_R_RADAR, px, 162, C_MUTED, 1);
  endFrame();
}

// ---------------- หน้า 4: อากาศตอนนี้ ----------------
void arcValue(int x, int y, float frac, uint32_t col) {
  frac = constrain(frac, 0.0f, 1.0f);
  if (frac > 0.01f) g->fillArc(x + 26, y + 24, 16, 19, 135, 135 + 270 * frac, col);
}
void drawNow() {
  beginFrame(BG_NOW);
  headerClock();
  if (!Wx || !Wx->valid) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  icon(ICONS_L, iconFor(w.weatherCode, w.isDay), 12, 26);
  String t = f0(w.temp) + "°";
  int x = drawGlyphs(GOLD_GLYPHS, t.c_str(), 138, 24);
  txt("C", x + 2, 40, 18, C_WHITE);
  ThaiId d = descFor(w.weatherCode);
  thai(d, 140, 94, RGB(225, 230, 240));
  thai(TH_FEELS, 140, 110, C_MUTED);
  txt(f0(w.feelsLike) + "°", 140 + thaiW(TH_FEELS) + 6, 110, 11, C_FEEL, middle_left);
  // การ์ด 4 ช่อง
  struct Card { String v; float f; uint32_t c; } cards[4];
  cards[0] = {w.humidity < 0 ? String("-") : String(w.humidity) + "%", w.humidity / 100.0f, RGB(90, 200, 255)};
  cards[1] = {f0(w.windSpeed), w.windSpeed / 40.0f, RGB(120, 255, 190)};
  if (A && A->valid) cards[2] = {f0(A->pm25), A->pm25 / 75.0f, pmColor(A->pm25)};
  else cards[2] = {"-", 0, C_MUTED};
  float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
  cards[3] = {f0(uv), uv / 11.0f, uvColor(uv)};
  for (int i = 0; i < 4; i++) {
    int cx = 12 + (i % 2) * 150, cy = 122 + (i / 2) * 54;
    arcValue(cx, cy, isnan(cards[i].f) ? 0 : cards[i].f, cards[i].c);
    txt(cards[i].v, cx + 54, cy + 26, 18, C_WHITE);
  }
  endFrame();
}

// ---------------- หน้า 3: พยากรณ์ ----------------
void drawForecast() {
  beginFrame(BG_FC);
  headerClock();
  if (!Wx || !Wx->valid || Wx->hCount < 2) {
    thai(TH_NO_DATA, 160, 120, C_MUTED, 1);
    endFrame();
    return;
  }
  const Weather& w = *Wx;
  int n = min(w.hCount, 13);
  const int x0 = 16, x1 = 304, y0 = 34, y1 = 92;
  float lo = 99, hi = -99;
  for (int i = 0; i < n; i++) if (!isnan(w.hTemp[i])) { lo = min(lo, w.hTemp[i]); hi = max(hi, w.hTemp[i]); }
  if (hi - lo < 3) { hi += 1.5f; lo -= 1.5f; }
  auto X = [&](int i) { return x0 + (float)i * (x1 - x0) / (n - 1); };
  auto Y = [&](float v) { return y0 + 12 + (hi - v) / (hi - lo) * (y1 - y0 - 18); };
  // พื้นที่ใต้กราฟไล่สีทอง
  for (int px = x0; px <= x1; px++) {
    float fi = (px - x0) * (float)(n - 1) / (x1 - x0);
    int i = min((int)fi, n - 2);
    float yy = Y(w.hTemp[i] + (w.hTemp[i + 1] - w.hTemp[i]) * (fi - i));
    for (int py = (int)yy; py < y1; py += 3) {
      uint8_t a = 120 - (py - yy) * 110 / max(1.0f, y1 - yy);
      g->fillRectAlpha(px, py, 1, 3, a, C_GOLD);
    }
  }
  for (int i = 1; i < n; i++) g->drawWideLine(X(i - 1), Y(w.hTemp[i - 1]), X(i), Y(w.hTemp[i]), 1.3f, RGB(240, 205, 130));
  for (int i = 0; i < n; i++) {
    int bx = X(i);
    int h = max(0, w.hRain[i]) * 22 / 100;
    if (h > 0) g->fillSmoothRoundRect(bx - 3, y1 + 28 - h, 6, h, 1, w.hRain[i] >= 60 ? RGB(90, 180, 255) : RGB(50, 90, 130));
    if (i % 3 == 0) {
      txt(f0(w.hTemp[i]) + "°", bx, Y(w.hTemp[i]) - 3, 9, C_WHITE, bottom_center);
      txt(hh(w.hHour[i]), bx, y1 + 33, 9, C_MUTED, top_center);
    }
  }
  // รายวัน: วันนี้ + 3 วัน
  struct Row { int code; float lo, hi; int rain; String date; } rows[4];
  rows[0] = {w.weatherCode, w.tempMin, w.tempMax, w.rainChance, ""};
  for (int i = 0; i < 3; i++) rows[i + 1] = {w.days[i].code, w.days[i].tmin, w.days[i].tmax, w.days[i].rainChance, w.days[i].date};
  float gmin = 99, gmax = -99;
  for (auto& r : rows) { if (!isnan(r.lo)) gmin = min(gmin, r.lo); if (!isnan(r.hi)) gmax = max(gmax, r.hi); }
  if (gmax - gmin < 1) gmax = gmin + 1;
  for (int i = 0; i < 4; i++) {
    int y = 140 + i * 24;
    ThaiId name = i == 0 ? TH_TODAY : i == 1 ? TH_TOMORROW : TH_WD_0;
    if (i >= 2) {
      int wd = WeatherApi::weekday(rows[i].date);
      name = wd >= 0 ? (ThaiId)(TH_WD_0 + wd) : TH_TOMORROW;
    }
    thai(name, 18, y + 9, i == 0 ? C_GOLD : C_WHITE);
    icon(ICONS_S, iconFor(rows[i].code, true), 89, y - 3);
    txt(rows[i].rain < 0 ? String("-") : String(rows[i].rain) + "%", 148, y + 9, 9, C_RAIN, middle_left);
    if (!isnan(rows[i].lo) && !isnan(rows[i].hi)) {
      int bx0 = 198 + (int)((rows[i].lo - gmin) / (gmax - gmin) * 70);
      int bx1 = 198 + (int)((rows[i].hi - gmin) / (gmax - gmin) * 70);
      if (bx1 - bx0 < 4) bx1 = bx0 + 4;
      for (int r = 0; r < 4; r++) g->drawGradientHLine(bx0, y + 7 + r, bx1 - bx0, RGB(90, 200, 255), RGB(255, 170, 90));
      txt(f0(rows[i].lo) + "°", 192, y + 9, 9, C_MUTED, middle_right);
      txt(f0(rows[i].hi) + "°", 304, y + 9, 11, C_WHITE, middle_right);
    }
  }
  endFrame();
}

void render() {
  screen = SCR_WEATHER;
  if (page == PAGE_HOME) drawHome();
  else if (page == PAGE_SUM) drawSummary();
  else if (page == PAGE_RADAR) drawRadar();
  else if (page == PAGE_NOW) drawNow();
  else drawForecast();
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

void showWeather(const Settings& s, const Weather& w, const AirQuality& a, const Market&) {
  S = &s;
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
  unsigned long interval = page == PAGE_HOME ? 200 : page == PAGE_RADAR ? 90 : 1000;
  if (screen == SCR_WEATHER && now - lastFrame >= interval) {
    lastFrame = now;
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
