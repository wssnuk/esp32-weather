#include "display.h"
#include <time.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#include "thai_text.h"

// ---------------- ขาของบอร์ด ESP32-2432S028 ----------------
// จอ (HSPI): SCK 14, MOSI 13, MISO 12, CS 15, DC 2, Backlight 21
// ทัชสกรีน XPT2046 (VSPI): CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36
// LED RGB: R 4, G 16, B 17 (ติดเมื่อ LOW)
#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17
#define PIN_SPEAKER 26

namespace {

// ST7789 แบบตั้งค่าน้อยที่สุด: ใช้ค่าแรงดัน/แกมมาจากโรงงานของจอเอง
// (ค่าเริ่มต้นของ LovyanGFX ทำให้จอบางล็อตสีเพี้ยน เช่น พื้นน้ำเงินเข้มกลายเป็นเขียว)
class Panel_ST7789_Basic : public lgfx::Panel_ST7789 {
 protected:
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
        0x01, 0 + CMD_INIT_DELAY, 150,        // software reset -> ค่าโรงงาน
        CMD_SLPOUT, 0 + CMD_INIT_DELAY, 130,  // ออกจาก sleep
        CMD_NORON, 0,
        CMD_DISPON, 0,
        0xFF, 0xFF,
    };
    return listno == 0 ? list0 : nullptr;
  }
};

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 ili;
  lgfx::Panel_ST7789 st;
  Panel_ST7789_Basic stBasic;
  lgfx::Bus_SPI bus;
  lgfx::Light_PWM light;
  lgfx::Touch_XPT2046 touch;

 public:
  explicit LGFX(uint8_t mode) {
    // โหมด: 0-1 ILI9341, 2-3 ST7789 ค่าโรงงาน, 4-5 ST7789 ค่า LovyanGFX (เลขคี่ = กลับสี)
    lgfx::Panel_Device* panel = (mode < 2)   ? (lgfx::Panel_Device*)&ili
                                : (mode < 4) ? (lgfx::Panel_Device*)&stBasic
                                             : (lgfx::Panel_Device*)&st;
    {
      auto cfg = bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      bus.config(cfg);
      panel->setBus(&bus);
    }
    {
      auto cfg = panel->config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.readable = true;
      cfg.invert = (mode % 2) == 1;
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      panel->config(cfg);
    }
    {
      auto cfg = light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light.config(cfg);
      panel->setLight(&light);
    }
    {
      auto cfg = touch.config();
      cfg.spi_host = VSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.x_min = 300; cfg.x_max = 3900;
      cfg.y_min = 200; cfg.y_max = 3800;
      touch.config(cfg);
      panel->setTouch(&touch);
    }
    setPanel(panel);
  }
};

LGFX* gfx = nullptr;

// ---------------- สี ----------------
// ใช้เฉพาะสีล้วน (แต่ละช่องสีเป็น 0 หรือ 255) — จอราคาประหยัดบางล็อตแสดงสีกลาง ๆ เพี้ยน
// แต่สีล้วนแสดงถูกต้องเสมอ ไม่ว่าจะตั้งค่าแกมมา/แรงดันของจอไว้แบบไหน
constexpr uint32_t C_BG = 0x000000;       // ดำ
constexpr uint32_t C_HEADER = 0x0000FF;   // น้ำเงิน
constexpr uint32_t C_TILE = 0x000000;     // พื้นช่องข้อมูล (ดำ + กรอบน้ำเงิน)
constexpr uint32_t C_BORDER = 0x0000FF;
constexpr uint32_t C_TEXT = 0xFFFFFF;     // ขาว
constexpr uint32_t C_MUTED = 0x00FFFF;    // ฟ้า
constexpr uint32_t C_LABEL = 0x00FFFF;    // ฟ้า
constexpr uint32_t C_ACCENT = 0x00FFFF;
constexpr uint32_t C_SUN = 0xFFFF00;      // เหลือง
constexpr uint32_t C_CLOUD = 0xFFFFFF;
constexpr uint32_t C_CLOUD_DARK = 0xFFFFFF;
constexpr uint32_t C_RAIN = 0x00FFFF;
constexpr uint32_t C_WARN = 0xFF0000;     // แดง

uint16_t c(uint32_t rgb) { return gfx->color888(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); }

// ---------------- ข้อความไทย (รูปภาพที่เรนเดอร์ไว้) ----------------
uint16_t thaiWidth(ThaiTextId id) { return THAI_TEXTS[id].w; }

// align: 0 = ซ้าย, 1 = กึ่งกลาง (x คือจุดกึ่งกลาง)
void drawThai(ThaiTextId id, int x, int y, uint32_t fg, uint32_t bg, int align = 0) {
  const ThaiText& t = THAI_TEXTS[id];
  if (align == 1) x -= t.w / 2;
  const int rowBytes = (t.w + 1) / 2;
  uint8_t fr = fg >> 16, fgG = (fg >> 8) & 0xFF, fb = fg & 0xFF;
  uint8_t br = bg >> 16, bgG = (bg >> 8) & 0xFF, bb = bg & 0xFF;
  uint16_t lut[16];
  for (int a = 0; a < 16; a++) {
    lut[a] = gfx->color565(br + (fr - br) * a / 15, bgG + (fgG - bgG) * a / 15, bb + (fb - bb) * a / 15);
  }
  gfx->startWrite();
  for (int yy = 0; yy < t.h; yy++) {
    const uint8_t* row = t.data + yy * rowBytes;
    for (int xx = 0; xx < t.w; xx++) {
      uint8_t v = pgm_read_byte(row + xx / 2);
      uint8_t a = (xx % 2) ? (v & 0x0F) : (v >> 4);
      if (a) gfx->writePixel(x + xx, y + yy, lut[a]);
    }
  }
  gfx->endWrite();
}

ThaiTextId thaiForCode(int code) {
  switch (code) {
    case 0: return TT_CLEAR;
    case 1: return TT_MAINLY_CLEAR;
    case 2: return TT_PARTLY_CLOUDY;
    case 3: return TT_OVERCAST;
    case 45: case 48: return TT_FOG;
    case 51: case 53: case 55: return TT_DRIZZLE;
    case 56: case 57: return TT_FREEZING_DRIZZLE;
    case 61: return TT_RAIN_LIGHT;
    case 63: return TT_RAIN_MODERATE;
    case 65: return TT_RAIN_HEAVY;
    case 66: case 67: return TT_FREEZING_RAIN;
    case 71: case 73: case 75: case 77: return TT_SNOW;
    case 80: return TT_SHOWERS_LIGHT;
    case 81: return TT_SHOWERS_MODERATE;
    case 82: return TT_SHOWERS_HEAVY;
    case 85: case 86: return TT_SNOW_SHOWERS;
    case 95: return TT_THUNDERSTORM;
    case 96: case 99: return TT_THUNDERSTORM_HAIL;
    default: return TT_UNKNOWN;
  }
}

// ---------------- ไอคอนสภาพอากาศ (วาดด้วยรูปทรง) ----------------
void drawSun(int cx, int cy, int r) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    gfx->drawWideLine(cx + cos(a) * (r + 5), cy + sin(a) * (r + 5), cx + cos(a) * (r + 13),
                      cy + sin(a) * (r + 13), 2.5f, c(C_SUN));
  }
  gfx->fillCircle(cx, cy, r, c(C_SUN));
}

void drawMoon(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, c(C_SUN));
  gfx->fillCircle(cx + r / 2, cy - r / 3, r, c(C_BG));
}

void drawCloud(int cx, int cy, uint32_t color) {
  uint16_t col = c(color);
  gfx->fillCircle(cx - 18, cy + 4, 14, col);
  gfx->fillCircle(cx + 2, cy - 8, 20, col);
  gfx->fillCircle(cx + 22, cy + 4, 14, col);
  gfx->fillRoundRect(cx - 32, cy + 2, 68, 18, 9, col);
}

void drawIcon(int code, bool isDay, int cx, int cy) {
  if (code == 0) {
    isDay ? drawSun(cx, cy, 22) : drawMoon(cx, cy, 24);
  } else if (code == 1 || code == 2) {
    isDay ? drawSun(cx - 14, cy - 12, 16) : drawMoon(cx - 14, cy - 14, 16);
    drawCloud(cx + 6, cy + 8, C_CLOUD);
  } else if (code == 3) {
    drawCloud(cx - 8, cy - 6, C_CLOUD_DARK);
    drawCloud(cx + 6, cy + 8, 0x000000);  // ขอบดำแยกเมฆสองก้อน
    drawCloud(cx + 8, cy + 10, C_CLOUD);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 4; i++)
      gfx->fillRoundRect(cx - 34 + (i % 2) * 8, cy - 18 + i * 12, 60, 6, 3, c(C_CLOUD_DARK));
  } else if (code >= 95) {
    drawCloud(cx, cy - 10, C_CLOUD_DARK);
    gfx->fillTriangle(cx + 2, cy + 6, cx - 10, cy + 24, cx + 2, cy + 24, c(C_SUN));
    gfx->fillTriangle(cx - 2, cy + 22, cx + 10, cy + 22, cx - 4, cy + 40, c(C_SUN));
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawCloud(cx, cy - 10, C_CLOUD);
    for (int i = 0; i < 3; i++) gfx->fillCircle(cx - 16 + i * 16, cy + 26 + (i % 2) * 6, 4, c(C_TEXT));
  } else if (code >= 51 && code <= 82) {
    drawCloud(cx, cy - 10, (code >= 63 && code != 80) ? C_CLOUD_DARK : C_CLOUD);
    for (int i = 0; i < 3; i++) {
      int x = cx - 16 + i * 16;
      gfx->drawWideLine(x + 4, cy + 18, x - 2, cy + 34, 2.5f, c(C_RAIN));
    }
  } else {
    gfx->setFont(&fonts::FreeSansBold18pt7b);
    gfx->setTextColor(c(C_MUTED));
    gfx->setTextDatum(middle_center);
    gfx->drawString("?", cx, cy);
  }
}

// ---------------- ส่วนช่วยวาด ----------------
void degree(int x, int y, int r, uint32_t color) {
  gfx->drawCircle(x, y, r, c(color));
  gfx->drawCircle(x, y, r - 1, c(color));
}

String fmt(float v, int digits) { return isnan(v) ? String("-") : String(v, digits); }

const char* compass(int deg) {
  static const char* d[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return deg < 0 ? "-" : d[((deg + 22) % 360) / 45];
}


// สถานะหน้าจอสำหรับนาฬิกา
bool weatherShown = false;
int lastMinute = -1;

void drawClock() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  if (t.tm_min == lastMinute) return;
  lastMinute = t.tm_min;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  gfx->fillRect(250, 0, 70, 26, c(C_HEADER));
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(middle_right);
  gfx->drawString(buf, 314, 13);
}

void clearScreen() {
  weatherShown = false;
  gfx->fillScreen(c(C_BG));
}

void title(const char* text) {
  gfx->fillRect(0, 0, 320, 26, c(C_HEADER));
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(middle_left);
  gfx->drawString(text, 8, 13);
}

// ---------------- หน้าจอข้อมูลอากาศ ----------------
constexpr int PAGE_COUNT = 7;
enum { PAGE_TODAY = 0, PAGE_NOW, PAGE_PM, PAGE_FORECAST, PAGE_HEAT, PAGE_GOLD, PAGE_SET };
int page = PAGE_TODAY;
const Market* curM = nullptr;
constexpr uint32_t C_UP = 0x00FF00;
constexpr uint32_t C_DOWN = 0xFF0000;
const Settings* curS = nullptr;
const Weather* curW = nullptr;
const AirQuality* curA = nullptr;
unsigned long wakeUntil = 0;
uint8_t brightnessNow = 0;
bool showingAlert = false;
unsigned long alertToggleAt = 0;

uint32_t aqColor(int level) {
  static const uint32_t cols[] = {0x00FFFF, 0x00FF00, 0xFFFF00, 0xFF0000, 0xFF00FF};
  return (level >= 0 && level <= 4) ? cols[level] : C_TEXT;
}

void drawHeader() {
  gfx->fillRect(0, 0, 320, 26, c(C_HEADER));
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(middle_left);
  static const ThaiTextId TITLES[PAGE_COUNT] = {TT_TITLE_TODAY, TT_TITLE_NOW,  TT_TITLE_PM, TT_TITLE_FORECAST,
                                                TT_TITLE_HEAT,  TT_TITLE_GOLD, TT_TITLE_SET};
  drawThai(TITLES[page], 8, 2, C_TEXT, C_HEADER);
  // จุดบอกหน้า
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 160 + i * 10;
    if (i == page) gfx->fillCircle(x, 13, 3, c(C_TEXT));
    else gfx->drawCircle(x, 13, 3, c(C_TEXT));
  }
  lastMinute = -1;
  drawClock();
}

// ---------- ตัวช่วยตัวเลข ----------
String withCommas(float v, int decimals) {
  if (isnan(v)) return "-";
  String s = String(fabs(v), decimals);
  int dot = s.indexOf('.');
  int intLen = dot < 0 ? s.length() : dot;
  String out;
  for (int i = 0; i < (int)s.length(); i++) {
    if (i > 0 && i < intLen && (intLen - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return (v < 0 ? "-" : "") + out;
}

String signedStr(float v, int decimals) {
  if (isnan(v)) return "-";
  return (v > 0 ? "+" : "") + withCommas(v, decimals);
}

uint32_t trendColor(float v) { return isnan(v) || v == 0 ? C_TEXT : (v > 0 ? C_UP : C_DOWN); }

// ---------- หน้า "วันนี้" ----------
int lastSecond = -1;

void drawBigClock(bool force) {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  if (!force && t.tm_sec == lastSecond) return;
  bool minuteChanged = force || t.tm_sec == 0 || lastSecond < 0;
  lastSecond = t.tm_sec;
  char buf[8];
  if (minuteChanged) {
    gfx->fillRect(0, 30, 320, 78, c(C_BG));
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    gfx->setFont(&fonts::Font8);
    gfx->setTextColor(c(C_TEXT));
    gfx->setTextDatum(top_left);
    gfx->drawString(buf, 10, 32);
  }
  snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_ACCENT), c(C_BG));
  gfx->setTextDatum(top_left);
  gfx->fillRect(272, 40, 46, 24, c(C_BG));
  gfx->drawString(buf, 274, 40);
}

void drawThaiDate() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  gfx->fillRect(0, 110, 320, 32, c(C_BG));
  int x = 8;
  ThaiTextId wd = (ThaiTextId)(TT_WDT_0 + t.tm_wday);
  drawThai(wd, x, 112, C_SUN, C_BG);
  x += thaiWidth(wd) + 4;
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_SUN));
  gfx->setTextDatum(top_left);
  String day = String(t.tm_mday);
  gfx->drawString(day, x, 117);
  x += gfx->textWidth(day) + 6;
  ThaiTextId mon = (ThaiTextId)(TT_MON_1 + t.tm_mon);
  drawThai(mon, x, 112, C_SUN, C_BG);
  x += thaiWidth(mon) + 6;
  gfx->drawString(String(t.tm_year + 1900 + 543), x, 117);
}

void drawTodayPage() {
  lastSecond = -1;
  drawBigClock(true);
  drawThaiDate();
  // กล่องอากาศย่อ
  gfx->drawRoundRect(4, 146, 152, 90, 8, c(C_BORDER));
  gfx->drawRoundRect(5, 147, 150, 88, 7, c(C_BORDER));
  if (curW && curW->valid) {
    drawIcon(curW->weatherCode, curW->isDay, 50, 184);
    gfx->setFont(&fonts::FreeSansBold18pt7b);
    gfx->setTextColor(c(C_TEXT));
    gfx->setTextDatum(top_left);
    String t = fmt(curW->temp, 0);
    gfx->drawString(t, 95, 152);
    degree(95 + gfx->textWidth(t) + 5, 157, 3, C_TEXT);
    drawThai(TT_MINI_RAIN, 95, 184, C_LABEL, C_BG);
    gfx->setFont(&fonts::FreeSansBold9pt7b);
    gfx->setTextColor(c(C_RAIN));
    gfx->drawString(curW->rainChance < 0 ? String("-") : String(curW->rainChance) + "%",
                    95 + thaiWidth(TT_MINI_RAIN) + 3, 188);
    if (curA && curA->valid) {
      gfx->setTextColor(c(aqColor(WeatherApi::pm25Level(curA->pm25))));
      gfx->drawString("PM " + fmt(curA->pm25, 0), 95, 212);
    }
  }
  // กล่องตลาดย่อ
  gfx->drawRoundRect(162, 146, 154, 90, 8, c(C_SUN));
  gfx->drawRoundRect(163, 147, 152, 88, 7, c(C_SUN));
  drawThai(TT_MINI_SET, 172, 149, C_LABEL, C_BG);
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextDatum(top_left);
  if (curM && curM->setValid) {
    gfx->setTextColor(c(C_TEXT));
    gfx->drawString(withCommas(curM->setPrice, 2), 172, 170);
    gfx->setTextColor(c(trendColor(curM->setChange)));
    gfx->setTextDatum(top_right);
    gfx->drawString(signedStr(curM->setPct, 2) + "%", 310, 170);
  }
  drawThai(TT_MINI_GOLD, 172, 191, C_LABEL, C_BG);
  gfx->setTextDatum(top_left);
  if (curM && curM->goldValid) {
    gfx->setTextColor(c(C_TEXT));
    gfx->drawString(withCommas(curM->barSell, 0), 172, 212);
    gfx->setTextColor(c(trendColor(curM->goldChange)));
    gfx->setTextDatum(top_right);
    if (!isnan(curM->goldChange)) gfx->drawString(signedStr(curM->goldChange, 0), 310, 212);
  }
}

// ---------- หน้าราคาทอง ----------
void goldRow(int y, ThaiTextId title, float buy, float sell) {
  drawThai(title, 8, y, C_SUN, C_BG);
  drawThai(TT_GOLD_BUY, 80, y + 24, C_LABEL, C_BG, 1);
  drawThai(TT_GOLD_SELL, 240, y + 24, C_LABEL, C_BG, 1);
  gfx->setFont(&fonts::FreeSansBold18pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(top_center);
  gfx->drawString(withCommas(buy, 0), 80, y + 44);
  gfx->drawString(withCommas(sell, 0), 240, y + 44);
}

void drawGoldPage() {
  if (!curM || !curM->goldValid) {
    drawThai(TT_NO_DATA, 160, 100, C_WARN, C_BG, 1);
    return;
  }
  const Market& m = *curM;
  goldRow(30, TT_GOLD_BAR, m.barBuy, m.barSell);
  gfx->drawFastHLine(8, 107, 304, c(C_BORDER));
  goldRow(110, TT_GOLD_ORN, m.ornBuy, m.ornSell);
  gfx->drawFastHLine(8, 187, 304, c(C_BORDER));
  // บรรทัดล่าง: เปลี่ยนแปลง + เวลา / ทองโลก + ดอลลาร์
  int x = 8;
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextDatum(top_left);
  if (m.goldEstimate) {
    drawThai(TT_GOLD_ESTIMATE, x, 191, 0xFF00FF, C_BG);
  } else {
    drawThai(TT_GOLD_CHANGE, x, 191, C_LABEL, C_BG);
    x += thaiWidth(TT_GOLD_CHANGE) + 6;
    gfx->setTextColor(c(trendColor(m.goldChange)));
    gfx->drawString(signedStr(m.goldChange, 0), x, 194);
  }
  if (m.goldTime.length()) {
    drawThai(TT_UPDATED, 200, 191, C_LABEL, C_BG);
    gfx->setTextColor(c(C_TEXT));
    gfx->drawString(m.goldTime, 204 + thaiWidth(TT_UPDATED), 194);
  }
  drawThai(TT_WORLD_GOLD, 8, 214, C_LABEL, C_BG);
  x = 12 + thaiWidth(TT_WORLD_GOLD);
  gfx->setTextColor(c(C_TEXT));
  String xs = "$" + withCommas(m.xau, 0);
  gfx->drawString(xs, x, 217);
  x += gfx->textWidth(xs) + 4;
  gfx->setTextColor(c(trendColor(m.xauPct)));
  gfx->drawString(signedStr(m.xauPct, 1) + "%", x, 217);
  drawThai(TT_USD_RATE, 200, 214, C_LABEL, C_BG);
  gfx->setTextColor(c(C_TEXT));
  gfx->drawString(String(m.usdthb, 2), 204 + thaiWidth(TT_USD_RATE), 217);
}

// ---------- หน้าหุ้นไทย ----------
void drawSetPage() {
  if (!curM || !curM->setValid) {
    drawThai(TT_NO_DATA, 160, 100, C_WARN, C_BG, 1);
    return;
  }
  const Market& m = *curM;
  uint32_t col = trendColor(m.setChange);
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_LABEL));
  gfx->setTextDatum(top_left);
  gfx->drawString("SET", 8, 32);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->drawString(withCommas(m.setPrice, 2), 8, 50);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(col));
  gfx->drawString(signedStr(m.setChange, 2) + "  (" + signedStr(m.setPct, 2) + "%)", 8, 94);
  // สถานะตลาด
  ThaiTextId st = m.setState == "open" ? TT_MKT_OPEN : m.setState == "break" ? TT_MKT_BREAK : TT_MKT_CLOSED;
  uint32_t stc = m.setState == "open" ? C_UP : m.setState == "break" ? C_SUN : 0xFF00FF;
  int sw = thaiWidth(st) + 16;
  gfx->drawRoundRect(312 - sw, 30, sw, 26, 8, c(stc));
  drawThai(st, 312 - sw / 2, 32, stc, C_BG, 1);
  // กราฟระหว่างวัน
  const int gx = 8, gy = 124, gw = 304, gh = 76;
  gfx->drawRect(gx, gy, gw, gh, c(C_BORDER));
  if (m.sparkCount >= 2) {
    float lo = m.setPrev, hi = m.setPrev;
    for (int i = 0; i < m.sparkCount; i++) { lo = min(lo, m.spark[i]); hi = max(hi, m.spark[i]); }
    if (hi - lo < 0.5f) { hi += 0.25f; lo -= 0.25f; }
    auto yOf = [&](float v) { return gy + gh - 4 - (int)((v - lo) / (hi - lo) * (gh - 8)); };
    int py = yOf(m.setPrev);  // เส้นปิดวันก่อน (เส้นประ)
    for (int x = gx + 2; x < gx + gw - 2; x += 6) gfx->drawFastHLine(x, py, 3, c(C_TEXT));
    for (int i = 1; i < m.sparkCount; i++) {
      int x0 = gx + 2 + (i - 1) * (gw - 4) / (m.sparkCount - 1);
      int x1 = gx + 2 + i * (gw - 4) / (m.sparkCount - 1);
      gfx->drawLine(x0, yOf(m.spark[i - 1]), x1, yOf(m.spark[i]), c(col));
      gfx->drawLine(x0, yOf(m.spark[i - 1]) + 1, x1, yOf(m.spark[i]) + 1, c(col));
    }
  }
  // บรรทัดล่าง
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextDatum(top_left);
  gfx->setTextColor(c(C_LABEL));
  gfx->drawString("SET50", 8, 210);
  gfx->setTextColor(c(C_TEXT));
  String s50 = withCommas(m.set50Price, 2);
  gfx->drawString(s50, 70, 210);
  gfx->setTextColor(c(trendColor(m.set50Change)));
  gfx->drawString(signedStr(m.set50Pct, 2) + "%", 76 + gfx->textWidth(s50), 210);
  // ปิดวันก่อน (ใต้ป้ายสถานะตลาด)
  drawThai(TT_PREV_CLOSE, 312 - thaiWidth(TT_PREV_CLOSE), 62, C_LABEL, C_BG);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(top_right);
  gfx->drawString(withCommas(m.setPrev, 2), 312, 84);
}

// ---------- ตัวช่วยของหน้าใหม่ ----------
void chip(ThaiTextId text, uint32_t col, int xRight, int y) {
  int w = thaiWidth(text) + 16;
  gfx->drawRoundRect(xRight - w, y, w, 26, 8, c(col));
  drawThai(text, xRight - w / 2, y + 2, col, C_BG, 1);
}
void hline(int y) { gfx->drawFastHLine(8, y, 304, c(C_BORDER)); }
String hh(int hour) {
  char b[6];
  snprintf(b, sizeof(b), "%02d:00", ((hour % 24) + 24) % 24);
  return b;
}
void valueCell(int cx, int y, ThaiTextId label, const String& value, uint32_t col, const GFXfont* font) {
  drawThai(label, cx, y, C_LABEL, C_BG, 1);
  gfx->setFont(font);
  gfx->setTextColor(c(col));
  gfx->setTextDatum(top_center);
  gfx->drawString(value, cx, y + 22);
}

void drawDescription() {
  gfx->fillRect(100, 82, 220, 28, c(C_BG));
  ThaiTextId small = (ThaiTextId)(thaiForCode(curW->weatherCode) - TT_CLEAR + TT_DS_CLEAR);
  if (showingAlert) drawThai(TT_RAIN_SOON_S, 100, 84, C_RAIN, C_BG);
  else drawThai(small, 100, 84, C_SUN, C_BG);
}

// ---------- หน้า 2: อากาศตอนนี้ ----------
void drawNowPage() {
  const Weather& w = *curW;
  drawIcon(w.weatherCode, w.isDay, 50, 68);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(top_left);
  String t = fmt(w.temp, 1);
  gfx->drawString(t, 100, 32);
  int tw = gfx->textWidth(t);
  degree(100 + tw + 8, 40, 5, C_TEXT);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->drawString("C", 100 + tw + 16, 34);
  drawThai(TT_LBL_HILO_SHORT, 276, 32, C_LABEL, C_BG, 1);
  gfx->setTextDatum(top_center);
  gfx->drawString(fmt(w.tempMax, 0) + "/" + fmt(w.tempMin, 0), 276, 54);
  showingAlert = false;
  drawDescription();
  hline(112);
  gfx->drawFastVLine(160, 116, 120, c(C_BORDER));
  hline(174);
  const GFXfont* big = &fonts::FreeSansBold18pt7b;
  String fl = fmt(w.feelsLike, 0);
  valueCell(80, 116, TT_LBL_FEELS_S, fl, C_SUN, big);
  degree(80 + gfx->textWidth(fl) / 2 + 7, 144, 4, C_SUN);
  valueCell(240, 116, TT_LBL_HUMID_S, w.humidity < 0 ? String("-") : String(w.humidity) + "%", C_TEXT, big);
  valueCell(80, 178, TT_LBL_RAIN_SHORT, w.rainChance < 0 ? String("-") : String(w.rainChance) + "%", C_RAIN, big);
  valueCell(240, 178, TT_LBL_WIND_S, fmt(w.windSpeed, 0) + " " + compass(w.windDir), C_TEXT, big);
}

// ---------- หน้า 3: ฝุ่น PM2.5 ----------
void drawPmPage() {
  if (!curA || !curA->valid) {
    drawThai(TT_FETCH_FAILED, 160, 100, C_WARN, C_BG, 1);
    return;
  }
  const AirQuality& a = *curA;
  int lv = WeatherApi::pm25Level(a.pm25);
  uint32_t col = aqColor(lv);
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_LABEL));
  gfx->setTextDatum(top_left);
  gfx->drawString("PM2.5", 8, 32);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(col));
  String v = fmt(a.pm25, 0);
  gfx->drawString(v, 8, 50);
  drawThai(TT_UNIT_UGM3, 8 + gfx->textWidth(v) + 8, 72, C_TEXT, C_BG);
  if (lv >= 0) {
    ThaiTextId lvText = (ThaiTextId)(TT_AQ_0 + lv);
    int w = thaiWidth(lvText) + 16;
    gfx->drawRoundRect(312 - w, 30, w, THAI_TEXTS[lvText].h + 6, 8, c(col));
    drawThai(lvText, 312 - w / 2, 33, col, C_BG, 1);
    ThaiTextId tip = (ThaiTextId)(TT_AQ_TIP_0 + lv);
    drawThai(tip, 312 - thaiWidth(tip), 96, C_TEXT, C_BG);
  }
  // กราฟฝุ่น 12 ชม. ข้างหน้า
  const int gx = 8, gy = 124, gw = 304, gh = 76;
  gfx->drawRect(gx, gy, gw, gh, c(C_BORDER));
  if (a.hPmCount > 1) {
    float hi = 10;
    int lowest = 0;
    for (int i = 0; i < a.hPmCount; i++) {
      hi = max(hi, a.hPm[i]);
      if (a.hPm[i] < a.hPm[lowest]) lowest = i;
    }
    int bw = (gw - 12) / a.hPmCount;
    for (int i = 0; i < a.hPmCount; i++) {
      int h = (int)(a.hPm[i] / hi * (gh - 26));
      gfx->fillRect(gx + 6 + i * bw, gy + gh - 4 - h, bw - 8, h, c(aqColor(WeatherApi::pm25Level(a.hPm[i]))));
    }
    drawThai(TT_NEXT12_S, gx + gw - 6 - thaiWidth(TT_NEXT12_S), gy + 3, C_LABEL, C_BG);
    drawThai(TT_PM_LOWEST, 312 - 52 - thaiWidth(TT_PM_LOWEST), 206, C_UP, C_BG);
    gfx->setFont(&fonts::FreeSansBold9pt7b);
    gfx->setTextColor(c(C_UP));
    gfx->setTextDatum(top_right);
    gfx->drawString(hh(a.hPmHour[lowest]), 312, 210);
  }
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextDatum(top_left);
  gfx->setTextColor(c(C_LABEL));
  gfx->drawString("PM10", 8, 210);
  gfx->setTextColor(c(C_TEXT));
  gfx->drawString(fmt(a.pm10, 0), 60, 210);
  gfx->setTextColor(c(C_LABEL));
  gfx->drawString("AQI", 104, 210);
  gfx->setTextColor(c(C_TEXT));
  gfx->drawString(a.usAqi < 0 ? String("-") : String(a.usAqi), 142, 210);
}

// ---------- หน้า 4: พยากรณ์ ----------
void drawForecastPage() {
  const Weather& w = *curW;
  int n = min(w.hCount, 12);
  drawThai(TT_NEXT12, 8, 28, C_SUN, C_BG);
  // ช่วงที่โอกาสฝนสูง (>= 60%)
  int r0 = -1, r1 = -1;
  for (int i = 0; i < n; i++) {
    if (w.hRain[i] >= 60) { if (r0 < 0) r0 = i; r1 = i; }
    else if (r0 >= 0) break;
  }
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextDatum(top_right);
  if (r0 >= 0) {
    String win = hh(w.hHour[r0]) + "-" + hh(w.hHour[r1] + 1);
    gfx->setTextColor(c(C_RAIN));
    gfx->drawString(win, 312, 31);
    drawThai(TT_RAIN_WINDOW, 312 - gfx->textWidth(win) - 6 - thaiWidth(TT_RAIN_WINDOW), 28, C_RAIN, C_BG);
  } else {
    drawThai(TT_RAIN_LOW, 312 - thaiWidth(TT_RAIN_LOW), 28, C_UP, C_BG);
  }
  const int gx = 8, gy = 52, gw = 304, gh = 76;
  gfx->drawRect(gx, gy, gw, gh, c(C_BORDER));
  if (n > 1) {
    float lo = 99, hi = -99;
    for (int i = 0; i < n; i++) if (!isnan(w.hTemp[i])) { lo = min(lo, w.hTemp[i]); hi = max(hi, w.hTemp[i]); }
    if (hi - lo < 2) { hi += 1; lo -= 1; }
    int step = (gw - 12) / n;
    auto xOf = [&](int i) { return gx + 6 + i * step + step / 2; };
    auto yOf = [&](float t) { return gy + 14 + (int)((hi - t) / (hi - lo) * (gh - 34)); };
    for (int i = 0; i < n; i++) {
      int h = max(0, w.hRain[i]) * (gh - 10) / 100 * 7 / 10;
      gfx->fillRect(xOf(i) - step / 2 + 4, gy + gh - 3 - h, step - 8, h, c(w.hRain[i] >= 60 ? C_RAIN : C_BORDER));
    }
    for (int i = 1; i < n; i++) {
      gfx->drawLine(xOf(i - 1), yOf(w.hTemp[i - 1]), xOf(i), yOf(w.hTemp[i]), c(C_SUN));
      gfx->drawLine(xOf(i - 1), yOf(w.hTemp[i - 1]) + 1, xOf(i), yOf(w.hTemp[i]) + 1, c(C_SUN));
    }
    gfx->setFont(&fonts::Font2);
    gfx->setTextDatum(bottom_center);
    gfx->setTextColor(c(C_SUN));
    for (int i = 0; i < n; i += 3) gfx->drawString(fmt(w.hTemp[i], 0), xOf(i), yOf(w.hTemp[i]) - 2);
    gfx->setTextDatum(top_center);
    gfx->setTextColor(c(C_TEXT));
    for (int i = 0; i < n; i += 4) gfx->drawString(hh(w.hHour[i]), xOf(i), gy + gh + 1);
  }
  // 3 วันข้างหน้า
  for (int i = 0; i < 3; i++) {
    const DayForecast& d = w.days[i];
    int cx = 56 + i * 104;
    int wd = WeatherApi::weekday(d.date);
    ThaiTextId name = (i == 0) ? TT_DAY_TOMORROW : (wd >= 0 ? (ThaiTextId)(TT_DAY_0 + wd) : TT_UNKNOWN);
    drawThai(name, cx, 146, C_SUN, C_BG, 1);
    gfx->setFont(&fonts::FreeSansBold12pt7b);
    gfx->setTextColor(c(C_TEXT));
    gfx->setTextDatum(top_center);
    gfx->drawString(fmt(d.tmax, 0) + "/" + fmt(d.tmin, 0), cx, 175);
    drawThai(TT_MINI_RAIN, cx - 4 - thaiWidth(TT_MINI_RAIN), 206, C_LABEL, C_BG);
    gfx->setFont(&fonts::FreeSansBold9pt7b);
    gfx->setTextColor(c(C_RAIN));
    gfx->setTextDatum(top_left);
    gfx->drawString(d.rainChance < 0 ? String("-") : String(d.rainChance) + "%", cx, 210);
    if (i > 0) gfx->drawFastVLine(4 + i * 104, 148, 88, c(C_BORDER));
  }
}

// ---------- หน้า 5: แดดและความร้อน ----------
uint32_t heatColor(int lv) {
  static const uint32_t cols[] = {0x00FF00, 0xFFFF00, 0xFF0000, 0xFF0000, 0xFF00FF};
  return (lv >= 0 && lv <= 4) ? cols[lv] : C_TEXT;
}
void drawHeatPage() {
  const Weather& w = *curW;
  // ดัชนีความร้อน
  drawThai(TT_HEAT_TITLE, 8, 28, C_SUN, C_BG);
  int hl = WeatherApi::heatLevel(w.feelsLike);
  uint32_t hc = heatColor(hl);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(hc));
  gfx->setTextDatum(top_left);
  String fv = fmt(w.feelsLike, 0);
  gfx->drawString(fv, 8, 52);
  degree(8 + gfx->textWidth(fv) + 8, 60, 5, hc);
  if (hl >= 0) {
    chip((ThaiTextId)(TT_HEAT_0 + hl), hc, 312, 52);
    ThaiTextId tip = (ThaiTextId)(TT_HEAT_TIP_0 + hl);
    drawThai(tip, 312 - thaiWidth(tip), 84, C_TEXT, C_BG);
  }
  hline(110);
  // รังสี UV (ชั่วโมงนี้)
  float uv = w.hCount > 0 && !isnan(w.hUv[0]) ? w.hUv[0] : w.uvMax;
  drawThai(TT_UV_TITLE, 8, 112, C_SUN, C_BG);
  int ul = WeatherApi::uvLevel(uv);
  uint32_t uc = heatColor(ul);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(uc));
  gfx->setTextDatum(top_left);
  String uvs = fmt(uv, 0);
  gfx->drawString(uvs, 8, 134);
  int ux = 8 + gfx->textWidth(uvs) + 12;
  if (!isnan(uv) && uv >= 3) {  // เวลาโดยประมาณที่ผิวคนไทยทั่วไปเริ่มไหม้ ~ 200/UV นาที
    drawThai(TT_BURN_IN, ux, 150, C_TEXT, C_BG);
    ux += thaiWidth(TT_BURN_IN) + 5;
    gfx->setFont(&fonts::FreeSansBold9pt7b);
    gfx->setTextColor(c(C_TEXT));
    String m = String((int)roundf(200.0f / uv));
    gfx->drawString(m, ux, 153);
    drawThai(TT_MINUTES, ux + gfx->textWidth(m) + 4, 150, C_TEXT, C_BG);
  }
  if (ul >= 0) {
    chip((ThaiTextId)(TT_UV_0 + ul), uc, 312, 134);
    ThaiTextId tip = (ThaiTextId)(TT_UV_TIP_0 + ul);
    drawThai(tip, 312 - thaiWidth(tip), 170, C_TEXT, C_BG);
  }
  hline(194);
  // ช่วงแดดแรง (UV >= 6) ใน 24 ชม. ข้างหน้า
  int s0 = -1, s1 = -1;
  for (int i = 0; i < w.hCount; i++) {
    if (!isnan(w.hUv[i]) && w.hUv[i] >= 6) { if (s0 < 0) s0 = i; s1 = i; }
    else if (s0 >= 0) break;
  }
  if (s0 >= 0) {
    drawThai(TT_AVOID_SUN, 8, 200, C_LABEL, C_BG);
    gfx->setFont(&fonts::FreeSansBold12pt7b);
    gfx->setTextColor(c(C_SUN));
    gfx->setTextDatum(top_right);
    gfx->drawString(hh(w.hHour[s0]) + " - " + hh(w.hHour[s1] + 1), 312, 202);
  } else {
    drawThai(TT_SUN_OK, 8, 200, C_UP, C_BG);
  }
}

void render() {
  if (!curS || !curW) return;
  clearScreen();
  drawHeader();
  if (!curW->valid && (page == PAGE_NOW || page == PAGE_PM || page == PAGE_FORECAST || page == PAGE_HEAT)) {
    drawThai(TT_FETCH_FAILED, 160, 100, C_WARN, C_BG, 1);
    gfx->setFont(&fonts::Font2);
    gfx->setTextColor(c(C_MUTED));
    gfx->setTextDatum(middle_center);
    gfx->drawString(curW->error, 160, 140);
  } else if (page == PAGE_NOW) {
    drawNowPage();
  } else if (page == PAGE_PM) {
    drawPmPage();
  } else if (page == PAGE_FORECAST) {
    drawForecastPage();
  } else if (page == PAGE_HEAT) {
    drawHeatPage();
  }
  if (page == PAGE_TODAY) drawTodayPage();
  else if (page == PAGE_GOLD) drawGoldPage();
  else if (page == PAGE_SET) drawSetPage();
  weatherShown = true;
}

// กลางคืน (22:00-06:00) หรี่จอ เว้นแต่เพิ่งแตะจอ
void applyBrightness() {
  uint8_t target = BRIGHTNESS_DAY;
  struct tm t;
  if (getLocalTime(&t, 0) && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR) &&
      millis() > wakeUntil)
    target = BRIGHTNESS_NIGHT;
  if (target != brightnessNow) {
    brightnessNow = target;
    gfx->setBrightness(target);
  }
}

// แตะจอ
unsigned long touchStart = 0;
bool longFired = false;

}  // namespace

namespace Display {

const char* modeName(uint8_t mode) {
  static const char* names[] = {"ILI9341",       "ILI9341 + invert", "ST7789",
                                "ST7789 + invert", "ST7789 alt",      "ST7789 alt + invert"};
  return names[mode % MODE_COUNT];
}

void begin(uint8_t mode) {
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setRgb(false, false, false);
  gfx = new LGFX(mode % MODE_COUNT);
  gfx->init();
  gfx->setRotation(1);  // แนวนอน 320x240
  gfx->setBrightness(BRIGHTNESS_DAY);
  brightnessNow = BRIGHTNESS_DAY;
  clearScreen();
}

void setRgb(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

void showBoot(const char* version) {
  clearScreen();
  drawSun(160, 80, 26);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(middle_center);
  gfx->drawString(APP_NAME, 160, 140);
  gfx->setFont(&fonts::Font2);
  gfx->setTextColor(c(C_MUTED));
  gfx->drawString(String("v") + version, 160, 165);
  drawThai(TT_BOOT_HINT, 160, 205, C_MUTED, C_BG, 1);
}

void showConnecting(const String& ssid) {
  clearScreen();
  title(APP_NAME);
  drawThai(TT_CONNECTING, 160, 95, C_TEXT, C_BG, 1);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_ACCENT));
  gfx->setTextDatum(middle_center);
  gfx->drawString(ssid, 160, 140);
}

void showWifiFailed() {
  gfx->fillRect(0, 170, 320, 40, c(C_BG));
  drawThai(TT_WIFI_FAILED, 160, 175, C_WARN, C_BG, 1);
}

void showPortal(const char* apSsid, const String& ip) {
  clearScreen();
  gfx->fillRect(0, 0, 320, 34, c(C_HEADER));
  drawThai(TT_SETUP_TITLE, 160, 2, C_TEXT, C_HEADER, 1);
  gfx->setTextDatum(middle_center);
  drawThai(TT_SETUP_1, 10, 44, C_TEXT, C_BG);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_SUN));
  gfx->drawString(apSsid, 160, 86);
  drawThai(TT_SETUP_2, 10, 104, C_TEXT, C_BG);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_ACCENT));
  gfx->drawString("http://" + ip, 160, 146);
  drawThai(TT_SETUP_3, 10, 164, C_TEXT, C_BG);
  drawThai(TT_SETUP_NOTE, 160, 210, C_MUTED, C_BG, 1);
}

void showLoading() {
  clearScreen();
  title(APP_NAME);
  drawThai(TT_LOADING, 160, 110, C_TEXT, C_BG, 1);
}

void showUpdating() {
  if (!weatherShown) return;
  gfx->fillRect(0, 26, 320, 3, c(C_ACCENT));
}

void showWeather(const Settings& s, const Weather& w, const AirQuality& a, const Market& m) {
  curS = &s;
  curW = &w;
  curA = &a;
  curM = &m;
  render();
}

void refresh() {
  if (weatherShown) render();
}

void nextPage() {
  wakeUntil = millis() + 30000;  // กลางคืน: แตะแล้วจอสว่างชั่วคราว
  applyBrightness();
  if (!curW) return;
  page = (page + 1) % PAGE_COUNT;
  render();
}

void beep() {
  for (int i = 0; i < 2; i++) {
    tone(PIN_SPEAKER, 2400, 120);
    delay(200);
  }
  noTone(PIN_SPEAKER);
}

void showDisplayMode(uint8_t mode) {
  clearScreen();
  drawThai(TT_DISPLAY_MODE, 160, 60, C_TEXT, C_BG, 1);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->setTextColor(c(C_SUN));
  gfx->setTextDatum(middle_center);
  gfx->drawString(String(mode + 1) + "/" + String(MODE_COUNT) + "  " + modeName(mode), 160, 115);
  drawThai(TT_DISPLAY_MODE_HINT, 160, 160, C_MUTED, C_BG, 1);
}

Touch loop() {
  if (weatherShown) drawClock();
  // สลับคำอธิบายอากาศกับคำเตือนฝนทุก 4 วินาที
  if (weatherShown && page == PAGE_TODAY) {
    drawBigClock(false);
    static int lastDay = -1;
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_mday != lastDay) { lastDay = t.tm_mday; drawThaiDate(); }
  }
  if (weatherShown && page == PAGE_NOW && curW && curW->valid && curW->rainNext2h >= RAIN_ALERT_PERCENT &&
      millis() > alertToggleAt) {
    alertToggleAt = millis() + 4000;
    showingAlert = !showingAlert;
    drawDescription();
  }
  static unsigned long lastBrightCheck = 0;
  if (millis() - lastBrightCheck > 5000) {
    lastBrightCheck = millis();
    applyBrightness();
  }
  int32_t x, y;
  bool pressed = gfx->getTouch(&x, &y) > 0;
  if (pressed) {
    if (touchStart == 0) {
      touchStart = millis();
      longFired = false;
    }
    if (!longFired && millis() - touchStart > 5000) {
      longFired = true;
      return Touch::LongPress;
    }
    return Touch::None;
  }
  if (touchStart != 0) {
    unsigned long held = millis() - touchStart;
    touchStart = 0;
    if (!longFired && held > 60) return Touch::Tap;
  }
  return Touch::None;
}

}  // namespace Display
