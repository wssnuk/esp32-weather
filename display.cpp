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

void tile(int x, int y, ThaiTextId label, const String& value, uint32_t valueColor = C_TEXT) {
  gfx->fillRoundRect(x, y, 102, 47, 8, c(C_TILE));
  gfx->drawRoundRect(x, y, 102, 47, 8, c(C_BORDER));
  gfx->drawRoundRect(x + 1, y + 1, 100, 45, 7, c(C_BORDER));
  drawThai(label, x + 51, y + 1, C_LABEL, C_TILE, 1);
  // ตัวเลขใหญ่ขึ้นถ้าใส่ช่องได้ ไม่งั้นใช้ขนาดเล็ก
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  if (gfx->textWidth(value) > 96) gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(valueColor));
  gfx->setTextDatum(bottom_center);
  gfx->drawString(value, x + 51, y + 44);
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
constexpr int PAGE_COUNT = 3;
int page = 0;
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
  String city = curS->city;
  int comma = city.indexOf(',');
  if (city.length() > 18 && comma > 0) city = city.substring(0, comma);
  if (city.length() > 18) city = city.substring(0, 17) + ".";
  gfx->drawString(city, 8, 13);
  // จุดบอกหน้า
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x = 214 + i * 11;
    if (i == page) gfx->fillCircle(x, 13, 3, c(C_TEXT));
    else gfx->drawCircle(x, 13, 3, c(C_TEXT));
  }
  lastMinute = -1;
  drawClock();
}

void drawDescription() {
  gfx->fillRect(0, 108, 320, 32, c(C_BG));
  if (showingAlert) drawThai(TT_RAIN_SOON, 160, 110, C_RAIN, C_BG, 1);
  else drawThai(thaiForCode(curW->weatherCode), 160, 110, C_SUN, C_BG, 1);
}

void drawNowPage() {
  const Weather& w = *curW;
  drawIcon(w.weatherCode, w.isDay, 62, 72);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(top_left);
  String t = fmt(w.temp, 1);
  gfx->drawString(t, 128, 38);
  int tw = gfx->textWidth(t);
  degree(128 + tw + 8, 46, 5, C_TEXT);
  gfx->setFont(&fonts::FreeSansBold12pt7b);
  gfx->drawString("C", 128 + tw + 16, 40);

  drawThai(TT_FEELS_LIKE, 130, 84, C_LABEL, C_BG);
  gfx->setFont(&fonts::FreeSansBold9pt7b);
  gfx->setTextColor(c(C_TEXT));
  gfx->setTextDatum(top_left);
  int fx = 130 + thaiWidth(TT_FEELS_LIKE) + 6;
  String f = fmt(w.feelsLike, 1);
  gfx->drawString(f, fx, 90);
  degree(fx + gfx->textWidth(f) + 4, 93, 2, C_TEXT);

  showingAlert = false;
  drawDescription();

  tile(4, 141, TT_LBL_HUMIDITY, w.humidity < 0 ? "-" : String(w.humidity));
  tile(109, 141, TT_LBL_WIND, fmt(w.windSpeed, 0) + " " + compass(w.windDir));
  tile(214, 141, TT_LBL_RAIN, w.rainChance < 0 ? "-" : String(w.rainChance));
  tile(4, 192, TT_LBL_HILO, fmt(w.tempMax, 0) + "/" + fmt(w.tempMin, 0));
  // ช่อง PM2.5 แทนที่ UV ถ้ามีข้อมูลฝุ่น (สีตามระดับ)
  if (curA && curA->valid) {
    int lv = WeatherApi::pm25Level(curA->pm25);
    tile(109, 192, TT_LBL_PM25_TILE, fmt(curA->pm25, 0), aqColor(lv));
  } else {
    tile(109, 192, TT_LBL_UV, fmt(w.uvMax, 1));
  }
  tile(214, 192, TT_LBL_SUN, w.sunrise + "-" + w.sunset);
}

void drawAirPage() {
  if (!curA || !curA->valid) {
    drawThai(TT_FETCH_FAILED, 160, 100, C_WARN, C_BG, 1);
    gfx->setFont(&fonts::Font2);
    gfx->setTextColor(c(C_MUTED));
    gfx->setTextDatum(middle_center);
    gfx->drawString(curA ? curA->error : String("-"), 160, 140);
    return;
  }
  const AirQuality& a = *curA;
  int lv = WeatherApi::pm25Level(a.pm25);
  uint32_t col = aqColor(lv);
  // ซ้าย: ค่า PM2.5 ตัวใหญ่
  drawThai(TT_LBL_PM25, 80, 30, C_LABEL, C_BG, 1);
  gfx->setFont(&fonts::FreeSansBold24pt7b);
  gfx->setTextColor(c(col));
  gfx->setTextDatum(middle_center);
  gfx->drawString(fmt(a.pm25, 0), 80, 80);
  drawThai(TT_UNIT_UGM3, 80, 106, C_MUTED, C_BG, 1);
  // ขวา: ระดับ
  gfx->drawRoundRect(162, 32, 154, 90, 10, c(col));
  gfx->drawRoundRect(163, 33, 152, 88, 9, c(col));
  drawThai(TT_AQ_LEVEL, 239, 38, C_MUTED, C_BG, 1);
  if (lv >= 0) {
    ThaiTextId lvText = (ThaiTextId)(TT_AQ_0 + lv);
    drawThai(lvText, 239, 70, col, C_BG, 1);
    drawThai((ThaiTextId)(TT_AQ_TIP_0 + lv), 160, 128, C_TEXT, C_BG, 1);
  }
  tile(4, 170, TT_LBL_PM10, fmt(a.pm10, 0));
  tile(109, 170, TT_LBL_AQI, a.usAqi < 0 ? String("-") : String(a.usAqi));
  tile(214, 170, TT_LBL_UV, fmt(curW->uvMax, 1));
}

void drawForecastPage() {
  for (int i = 0; i < 3; i++) {
    const DayForecast& d = curW->days[i];
    int x = 4 + i * 105, cx = x + 51;
    gfx->drawRoundRect(x, 31, 102, 206, 8, c(C_BORDER));
    gfx->drawRoundRect(x + 1, 32, 100, 204, 7, c(C_BORDER));
    int wd = WeatherApi::weekday(d.date);
    ThaiTextId name = (i == 0) ? TT_DAY_TOMORROW : (wd >= 0 ? (ThaiTextId)(TT_DAY_0 + wd) : TT_UNKNOWN);
    drawThai(name, cx, 36, C_SUN, C_BG, 1);
    if (d.code >= 0) drawIcon(d.code, true, cx, 98);
    drawThai(TT_LBL_HILO_SHORT, cx, 140, C_LABEL, C_BG, 1);
    gfx->setFont(&fonts::FreeSansBold12pt7b);
    gfx->setTextColor(c(C_TEXT));
    gfx->setTextDatum(top_center);
    gfx->drawString(fmt(d.tmax, 0) + "/" + fmt(d.tmin, 0), cx, 162);
    drawThai(TT_LBL_RAIN_SHORT, cx, 186, C_LABEL, C_BG, 1);
    gfx->setTextColor(c(C_RAIN));
    gfx->drawString(d.rainChance < 0 ? String("-") : String(d.rainChance) + "%", cx, 208);
  }
}

void render() {
  if (!curS || !curW) return;
  clearScreen();
  drawHeader();
  if (!curW->valid) {
    drawThai(TT_FETCH_FAILED, 160, 100, C_WARN, C_BG, 1);
    gfx->setFont(&fonts::Font2);
    gfx->setTextColor(c(C_MUTED));
    gfx->setTextDatum(middle_center);
    gfx->drawString(curW->error, 160, 140);
  } else if (page == 0) {
    drawNowPage();
  } else if (page == 1) {
    drawAirPage();
  } else {
    drawForecastPage();
  }
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

void showWeather(const Settings& s, const Weather& w, const AirQuality& a) {
  curS = &s;
  curW = &w;
  curA = &a;
  render();
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
  if (weatherShown && page == 0 && curW && curW->valid && curW->rainNext2h >= RAIN_ALERT_PERCENT &&
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
