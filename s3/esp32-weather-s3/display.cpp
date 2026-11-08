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
#include "config.h"

// ---------------- ขาของบอร์ด (ยืนยันจากโปรแกรมตรวจบอร์ด: จอแบบที่ 1 + กลับสี) ----------------
#define PIN_SCK 12
#define PIN_MOSI 11
#define PIN_MISO 13
#define PIN_CS 10
#define PIN_DC 46
#define PIN_BL 45

namespace {

// ชุดคำสั่งเริ่มจอแบบเดียวกับโปรแกรมตรวจบอร์ดที่ใช้งานได้จริง
class PanelS3 : public lgfx::Panel_ILI9341 {
 protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
        0x01, 0 + CMD_INIT_DELAY, 150,          // software reset
        0xCF, 3, 0x00, 0xC1, 0x30,              // power control B
        0xC0, 1, 0x23,                          // power control 1
        0xC1, 1, 0x10,                          // power control 2
        0xC5, 2, 0x3E, 0x28,                    // VCOM
        CMD_SLPOUT, 0 + CMD_INIT_DELAY, 120,
        CMD_NORON, 0,
        CMD_DISPON, 0,
        0xFF, 0xFF,
    };
    return listno == 0 ? list0 : nullptr;
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
const uint32_t C_WHITE = RGB(255, 255, 255), C_GOLD = RGB(232, 196, 120), C_MUTED = RGB(170, 180, 200),
               C_TEAL = RGB(90, 230, 210), C_RAIN = RGB(120, 190, 255), C_FEEL = RGB(255, 170, 120),
               C_UV = RGB(255, 140, 210), C_RED = RGB(255, 110, 90);

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
void endFrame() {
  if (canvas) canvas->pushSprite(gfx, 0, 0);
  else gfx->endWrite();
}

void headerClock() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
  txt(b, 308, 12, 11, C_WHITE, middle_right);
}

// ---------------- สถานะ ----------------
const Settings* S = nullptr;
const Weather* Wx = nullptr;
const AirQuality* A = nullptr;
constexpr int PAGE_COUNT = 3;
enum { PAGE_HOME = 0, PAGE_NOW, PAGE_FC };
int page = PAGE_HOME;
enum Screen { SCR_NONE, SCR_WEATHER, SCR_MESSAGE } screen = SCR_NONE;
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

void drawTicker() {
  const Weather& w = *Wx;
  int code = w.weatherCode;
  bool rainingNow = code >= 51 && code != 45 && code != 48 && code < 100;
  int at = -1;
  for (int i = 0; i < w.hCount && i < 12; i++)
    if (w.hRain[i] >= 60) { at = i; break; }
  uint32_t dot = C_TEAL;
  int x = 38, y = 216;
  if (updating) {
    thai(TH_T_UPDATING, x, y, C_MUTED);
  } else if (rainingNow) {
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
  g->fillSmoothCircle(26, 216, 4, dot);
}

void drawHome() {
  beginFrame(BG_HOME);
  struct tm t;
  bool haveTime = getLocalTime(&t, 0);
  String city = S ? S->city : String("");
  int comma = city.indexOf(',');
  if (comma > 0) city = city.substring(0, comma);
  txt(city, 14, 12, 11, C_GOLD, middle_left);
  drawWifi(290, 16);
  char hm[6] = "--:--", sec[4] = ":--";
  if (haveTime) {
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(sec, sizeof(sec), ":%02d", t.tm_sec);
  }
  int x = drawGlyphs(GOLD_GLYPHS, hm, 18, 26);
  drawGlyphs(SEC_GLYPHS, sec, x + 2, 26 + GOLD_ASC - SEC_ASC - 2);
  // แถบวินาที (เลื่อนลื่น ๆ)
  if (haveTime) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    float frac = (t.tm_sec + tv.tv_usec / 1e6f) / 60.0f;
    int w = (int)(280 * frac);
    if (w > 2) {
      for (int r = 0; r < 3; r++) g->drawGradientHLine(20, 100 + r, w, C_TEAL, C_GOLD);
      g->fillSmoothCircle(20 + w, 101, 3, C_WHITE);
    }
    // วันที่ไทย
    int dx = 20;
    ThaiId wd = (ThaiId)(TH_WDT_0 + t.tm_wday);
    thai(wd, dx, 116, C_WHITE);
    dx += thaiW(wd) + 4;
    txt(String(t.tm_mday), dx, 116, 14, C_WHITE, middle_left);
    dx += txtW(String(t.tm_mday), 14) + 5;
    ThaiId mon = (ThaiId)(TH_MON_0 + t.tm_mon);
    thai(mon, dx, 116, C_WHITE);
    dx += thaiW(mon) + 5;
    txt(String(t.tm_year + 1900 + 543), dx, 116, 14, C_WHITE, middle_left);
    txt("UTC+7", 300, 116, 9, C_MUTED, middle_right);
  }
  if (Wx && Wx->valid) {
    const Weather& w = *Wx;
    icon(ICONS_M, iconFor(w.weatherCode, w.isDay), 14, 145);
    txt(f0(w.temp) + "°", 74, 167, 24, C_WHITE, middle_left);
    txt(f0(w.feelsLike) + "°", 150, 158, 14, C_FEEL, middle_center);
    txt(w.rainChance < 0 ? String("-") : String(w.rainChance) + "%", 190, 158, 14, C_RAIN, middle_center);
    if (A && A->valid) txt(f0(A->pm25), 230, 158, 14, pmColor(A->pm25), middle_center);
    float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
    txt(f0(uv), 270, 158, 14, uvColor(uv), middle_center);
    drawTicker();
  } else {
    thai(TH_NO_DATA, 160, 167, C_MUTED, 1);
  }
  endFrame();
}

// ---------------- หน้า 2: อากาศตอนนี้ ----------------
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
  unsigned long interval = page == PAGE_HOME ? 200 : 1000;
  if (screen == SCR_WEATHER && now - lastFrame >= interval) {
    lastFrame = now;
    render();
  }
  static unsigned long lastBright = 0;
  if (now - lastBright > 5000) {
    lastBright = now;
    applyBrightness();
  }
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
