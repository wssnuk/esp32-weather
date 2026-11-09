// =====================================================================
//  ESP32 Weather Monitor — รายงานสภาพอากาศปัจจุบัน
//  - ข้อมูลจาก Open-Meteo (ฟรี ไม่ต้องสมัคร ไม่ต้องใช้ API key)
//  - บอร์ด: ESP32-2432S028 (Cheap Yellow Display) จอสัมผัส 2.8"
//  - แสดงผล 3 ทาง: จอบนบอร์ด, หน้าเว็บบนมือถือ/คอม, Serial Monitor (115200)
//  - แตะจอ = อัปเดตทันที | แตะค้าง 5 วินาที = เปลี่ยนโหมดจอ (ถ้าสีเพี้ยน)
//  - ครั้งแรก: บอร์ดปล่อย Wi-Fi "ESP32-Weather-Setup" ให้เข้าไปกรอกรหัส Wi-Fi บ้าน
//  - คำสั่งใน Serial Monitor: help / now / city <ชื่อเมือง> / wifi / status
//  - กดปุ่ม BOOT ค้าง 3 วินาที = ล้างการตั้งค่า Wi-Fi
// =====================================================================
#include <WiFi.h>
#include <Preferences.h>
#include "app.h"
#include "config.h"
#include "display.h"
#include "improv.h"
#include "autoupdate.h"
#include "radar.h"
#include "geo.h"
#include "net.h"
#include <esp_system.h>

// งานหลักใช้ HTTPS + JSON + วาดจอ → ขยาย stack จาก 8 KB เป็น 16 KB กันล้น
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#include "sound.h"
#include "web.h"

Settings settings;
Weather weather;
AirQuality air;
Market market;
String lastResetInfo;  // สาเหตุการรีสตาร์ทครั้งก่อน (ถ้าค้าง)
static bool lowPower = false;  // ครั้งก่อนไฟตก → ลดกำลังส่ง Wi-Fi / ไม่เล่นเสียงตอนเปิดเครื่อง
static unsigned long nextMarketAt = 0;

static bool portalMode = false;
static unsigned long portalStartedAt = 0;
static unsigned long nextWeatherAt = 0;
static unsigned long buttonDownAt = 0;
static String serialLine;
static int wifiSetupStep = 0;   // 0 = ปกติ, 1 = รอชื่อ Wi-Fi, 2 = รอรหัสผ่าน
static String newSsid;

// ---------------- แสดงผลทาง Serial ----------------
static String fmt(float v, int digits = 1) { return isnan(v) ? String("-") : String(v, digits); }

static void printWeatherReport() {
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.printf("  🌏 รายงานสภาพอากาศ : %s\n", settings.city.c_str());
  if (!weather.valid) {
    Serial.printf("  ❌ ยังไม่มีข้อมูล (%s)\n", weather.error.c_str());
    Serial.println(F("=================================================="));
    return;
  }
  String t = weather.time;
  t.replace("T", " ");
  Serial.printf("  🕒 ข้อมูล ณ        : %s (%s)\n", t.c_str(), weather.timezone.c_str());
  Serial.println(F("--------------------------------------------------"));
  Serial.printf("  %s สภาพอากาศ      : %s\n", WeatherApi::icon(weather.weatherCode, weather.isDay),
                WeatherApi::describe(weather.weatherCode));
  Serial.printf("  🌡️ อุณหภูมิ        : %s °C (รู้สึกเหมือน %s °C)\n", fmt(weather.temp).c_str(),
                fmt(weather.feelsLike).c_str());
  Serial.printf("  📈 สูงสุด/ต่ำสุด    : %s / %s °C\n", fmt(weather.tempMax).c_str(),
                fmt(weather.tempMin).c_str());
  Serial.printf("  💧 ความชื้น        : %d %%\n", weather.humidity);
  Serial.printf("  🌧️ ฝนขณะนี้        : %s มม. | โอกาสฝนวันนี้ %d %%\n",
                fmt(weather.precipitation).c_str(), weather.rainChance);
  Serial.printf("  💨 ลม             : %s กม./ชม. จากทิศ%s\n", fmt(weather.windSpeed).c_str(),
                WeatherApi::windDirection(weather.windDir));
  Serial.printf("  ☁️ เมฆปกคลุม       : %d %%\n", weather.cloudCover);
  Serial.printf("  🧭 ความกดอากาศ    : %s hPa\n", fmt(weather.pressure, 0).c_str());
  Serial.printf("  🔆 UV สูงสุดวันนี้   : %s\n", fmt(weather.uvMax).c_str());
  Serial.printf("  🌅 พระอาทิตย์ขึ้น/ตก : %s / %s\n", weather.sunrise.c_str(), weather.sunset.c_str());
  if (air.valid) {
    int lv = WeatherApi::pm25Level(air.pm25);
    Serial.printf("  😷 ฝุ่น PM2.5      : %s มคก./ลบ.ม. (%s) | PM10 %s | AQI %d\n", fmt(air.pm25, 0).c_str(),
                  WeatherApi::pm25LevelName(lv), fmt(air.pm10, 0).c_str(), air.usAqi);
  }
  if (weather.rainNext2h >= RAIN_ALERT_PERCENT)
    Serial.printf("  ☔ เตือน: โอกาสฝน %d%% ภายใน 2 ชั่วโมง\n", weather.rainNext2h);
  for (int i = 0; i < 3; i++) {
    const DayForecast& d = weather.days[i];
    if (d.date.isEmpty()) continue;
    Serial.printf("  📅 %s : %s %s/%s °C ฝน %d%%\n", d.date.c_str(), WeatherApi::describe(d.code),
                  fmt(d.tmax, 0).c_str(), fmt(d.tmin, 0).c_str(), d.rainChance);
  }
  Serial.println(F("--------------------------------------------------"));
  Serial.printf("  เว็บ: http://%s/  หรือ  http://%s.local/\n", WiFi.localIP().toString().c_str(),
                DEVICE_HOSTNAME);
  Serial.println(F("=================================================="));
}

static void printHelp() {
  Serial.println(F("\nคำสั่งที่ใช้ได้ (พิมพ์แล้วกด Enter):"));
  Serial.println(F("  now            อัปเดตสภาพอากาศทันที"));
  Serial.println(F("  city <ชื่อ>     เปลี่ยนเมือง เช่น city Chiang Mai"));
  Serial.println(F("  status         ดูสถานะบอร์ด"));
  Serial.println(F("  setwifi        ตั้งค่า Wi-Fi ผ่านหน้านี้ (ไม่ต้องใช้มือถือ)"));
  Serial.println(F("  wifi           ล้างการตั้งค่า Wi-Fi แล้วเข้าโหมดตั้งค่า"));
  Serial.println(F("  display        หมุนจอ (ถ้าภาพกลับด้าน)"));
  Serial.println(F("  preset         เลือกโหมดภาพใหม่ (ถ้าจอซีด/ไม่สว่าง) หรือ preset 1-4"));
  Serial.println(F("  vivid 0-3      เร่งสีและความสว่าง (0 ปิด, 3 จัดเต็ม)"));
  Serial.println(F("  autoloc        หาตำแหน่งอัตโนมัติจากอินเทอร์เน็ตอีกครั้ง"));
  Serial.println(F("  sound          ทดสอบลำโพงใหม่"));
  Serial.println(F("  chime          เล่นเสียงเตือนฝนให้ฟัง"));
  Serial.println(F("  help           แสดงคำสั่ง"));
}

// ---------------- ฟังก์ชันที่ใช้ร่วมกับหน้าเว็บ ----------------
void requestWeatherRefresh() { nextWeatherAt = millis(); }

static void updateAddress();

bool changeCity(const String& nameIn, String& message) {
  String name = nameIn;
  name.trim();
  if (name.isEmpty()) {
    message = "กรุณาใส่ชื่อเมือง";
    return false;
  }
  double lat, lon;
  String display;
  if (!WeatherApi::geocode(name, lat, lon, display)) {
    message = "ไม่พบเมือง \"" + name + "\" ลองสะกดเป็นภาษาอังกฤษ เช่น Chiang Mai";
    return false;
  }
  settings.city = display;
  settings.lat = lat;
  settings.lon = lon;
  Store::saveLocation(display, lat, lon);
  if (!settings.locManual) { settings.locManual = true; Store::saveLocManual(true); }
  updateAddress();
  Radar::setLocation(lat, lon);
  weather.valid = false;
  requestWeatherRefresh();
  message = "ตั้งเป็น " + display + " (" + String(lat, 3) + ", " + String(lon, 3) + ") แล้ว";
  Serial.println("[City] " + message);
  return true;
}

// อัปเดตชื่อพื้นที่ภาษาไทยของพิกัดปัจจุบัน
static void updateAddress() {
  Geo::Address a;
  String err;
  if (Geo::reverse(settings.lat, settings.lon, a, err)) {
    settings.addrTh = Geo::shortThai(a);
    Store::saveAddress(settings.addrTh);
    if (!settings.locManual && a.latinName.length()) {
      settings.city = a.latinName;
      Store::saveLocation(settings.city, settings.lat, settings.lon);
    }
  } else {
    Serial.println("[Geo] " + err);
  }
}

void autoLocation(bool force) {
  g_stage = ST_GEO;
  if (settings.locManual && !force) {
    if (settings.addrTh.isEmpty()) updateAddress();
    return;
  }
  if (force) { settings.locManual = false; Store::saveLocManual(false); }
  Geo::Fix f;
  String err;
  if (!Geo::locate(f, err)) {
    Serial.println("[Geo] " + err + " — ใช้ตำแหน่งเดิม");
    if (settings.addrTh.isEmpty()) updateAddress();
    return;
  }
  // ตำแหน่งจาก IP หยาบมาก: ถ้าเดิมมีพิกัดที่ห่างไม่เกิน 30 กม. ให้ใช้ของเดิม
  double dy = (f.lat - settings.lat) * 111.0, dx = (f.lon - settings.lon) * 111.0 * cos(f.lat * DEG_TO_RAD);
  bool moved = dx * dx + dy * dy > 0.05 * 0.05;
  if (!f.fromWifi && dx * dx + dy * dy < 30.0 * 30.0 && !settings.addrTh.isEmpty()) moved = false;
  if (moved || settings.addrTh.isEmpty()) {
    settings.lat = f.lat;
    settings.lon = f.lon;
    Store::saveLocation(settings.city, f.lat, f.lon);
    updateAddress();
    Radar::setLocation(settings.lat, settings.lon);
    weather.valid = false;
    requestWeatherRefresh();
  }
}

bool setExactLocation(double lat, double lon, const String& name, String& message) {
  if (isnan(lat) || isnan(lon) || fabs(lat) > 85 || fabs(lon) > 180) {
    message = "พิกัดไม่ถูกต้อง";
    return false;
  }
  // จอแสดงได้เฉพาะตัวอักษรละติน
  String n;
  for (char c : name) if (c >= 32 && c < 127) n += c;
  n.trim();
  if (n.isEmpty()) n = "My location";
  if (n.length() > 28) n = n.substring(0, 28);
  settings.city = n;
  settings.lat = lat;
  settings.lon = lon;
  settings.locManual = true;
  Store::saveLocManual(true);
  Store::saveLocation(n, lat, lon);
  updateAddress();
  Radar::setLocation(lat, lon);
  weather.valid = false;
  requestWeatherRefresh();
  message = "ตั้งตำแหน่งเป็น " + n + " (" + String(lat, 4) + ", " + String(lon, 4) + ") แล้ว";
  Serial.println("[Location] " + message);
  return true;
}

static void nextDisplayMode() {
  uint8_t mode = (settings.displayMode + 1) % Display::MODE_COUNT;
  Store::saveDisplayMode(mode);
  Serial.printf("[Display] เปลี่ยนเป็นโหมด %u: %s — กำลังรีสตาร์ท\n", mode + 1, Display::modeName(mode));
  Display::showDisplayMode(mode);  // แสดงด้วยโหมดเดิม เผื่อยังอ่านได้
  delay(1500);
  ESP.restart();
}

void resetWifiAndRestart() {
  Serial.println(F("[WiFi] ล้างการตั้งค่า Wi-Fi แล้ว กำลังรีสตาร์ท..."));
  Store::clearWifi();
  delay(500);
  ESP.restart();
}

// ---------------- Wi-Fi ----------------
static bool connectWifi() {
  if (settings.wifiSsid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.setTxPower(lowPower ? WIFI_POWER_11dBm : WIFI_POWER_15dBm);  // ลดกระแสกระชาก กันบอร์ดรีเซ็ตเองเมื่อไฟ USB อ่อน
  WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
  Serial.printf("[WiFi] กำลังเชื่อมต่อ \"%s\" ", settings.wifiSsid.c_str());
  Display::showConnecting(settings.wifiSsid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    Display::setRgb(true, true, false);  // เหลือง = กำลังเชื่อมต่อ
    delay(150);
    Display::setRgb(false, false, false);
    delay(100);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] เชื่อมต่อไม่สำเร็จ — ตรวจรหัสผ่าน และต้องเป็น Wi-Fi 2.4 GHz"));
    Serial.printf("[WiFi] สถานะ %d\n", WiFi.status());
    Display::showWifiFailedReason(WiFi.status());
    delay(3500);
    return false;
  }
  Serial.printf("[WiFi] เชื่อมต่อแล้ว ✅ IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static void startPortal() {
  portalMode = true;
  portalStartedAt = millis();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID);
  Web::beginPortal();
  WiFi.setTxPower(lowPower ? WIFI_POWER_11dBm : WIFI_POWER_15dBm);
  Display::beginWifiSetup(SETUP_AP_SSID, WiFi.softAPIP().toString());
  Serial.println(F("\n================ โหมดตั้งค่า ================"));
  Serial.println(F(" 1) ใช้โทรศัพท์เชื่อมต่อ Wi-Fi ชื่อ: " SETUP_AP_SSID));
  Serial.printf(" 2) หน้าตั้งค่าจะเด้งขึ้นเอง (ถ้าไม่เด้ง เปิด http://%s/)\n",
                WiFi.softAPIP().toString().c_str());
  Serial.println(F(" 3) เลือก Wi-Fi บ้าน ใส่รหัสผ่าน และชื่อเมือง แล้วกดบันทึก"));
  Serial.println(F(" หรือพิมพ์คำสั่ง setwifi ตรงนี้แล้วกด Enter เพื่อตั้งค่าผ่านคอม"));
  Serial.println(F("=============================================="));
}

// ---------------- อินพุต ----------------
static void processChar(char c) {
  {
    // Enter อาจส่งมาเป็น \r, \n หรือ \r\n แล้วแต่โปรแกรม — รองรับทุกแบบ
    static bool lastWasCR = false;
    if (c == '\n' && lastWasCR) { lastWasCR = false; return; }
    lastWasCR = (c == '\r');
    if (c == '\r') c = '\n';
    if (c == 8 || c == 127) {  // Backspace
      if (serialLine.length()) { serialLine.remove(serialLine.length() - 1); Serial.print("\b \b"); }
      return;
    }
    if (c != '\n') {
      if (serialLine.length() < 100) { serialLine += c; Serial.print(c); }  // แสดงตัวที่พิมพ์
      return;
    }
    Serial.println();
    String cmd = serialLine;
    serialLine = "";
    cmd.trim();
    if (wifiSetupStep == 1) {
      if (cmd.isEmpty()) { Serial.println(F("ชื่อ Wi-Fi ว่างไม่ได้ พิมพ์ใหม่:")); return; }
      newSsid = cmd;
      wifiSetupStep = 2;
      Serial.println("ชื่อ Wi-Fi: \"" + newSsid + "\"");
      Serial.println(F("พิมพ์รหัสผ่าน Wi-Fi แล้วกด Enter (ไม่มีรหัส ให้กด Enter เลย):"));
      return;
    }
    if (wifiSetupStep == 2) {
      wifiSetupStep = 0;
      Store::saveWifi(newSsid, cmd);
      Serial.println(F("บันทึกแล้ว ✅ กำลังรีสตาร์ทเพื่อเชื่อมต่อ..."));
      delay(800);
      ESP.restart();
    }
    if (cmd.isEmpty()) return;
    String lower = cmd;
    lower.toLowerCase();
    if (lower == "help") printHelp();
    else if (lower == "now") { Serial.println(F("กำลังอัปเดต...")); requestWeatherRefresh(); }
    else if (lower == "wifi") resetWifiAndRestart();
    else if (lower == "setwifi") {
      wifiSetupStep = 1;
      Serial.println(F("\nพิมพ์ชื่อ Wi-Fi (SSID) ให้ตรงทุกตัวอักษร แล้วกด Enter:"));
      int n = WiFi.scanNetworks();
      if (n > 0) {
        Serial.println(F("Wi-Fi ที่เจอแถวนี้:"));
        for (int i = 0; i < n && i < 15; i++)
          Serial.printf("  - %s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        Serial.println(F("พิมพ์ชื่อ Wi-Fi:"));
      }
      WiFi.scanDelete();
    }
    else if (lower == "display") nextDisplayMode();
    else if (lower == "preset") Display::choosePreset();
    else if (lower.startsWith("preset ")) { Display::setPreset(cmd.substring(7).toInt() - 1); Serial.printf("[Display] โหมดภาพ %u\n", Display::preset() + 1); }
    else if (lower.startsWith("vivid ")) { Display::setVivid(cmd.substring(6).toInt()); Serial.printf("[Display] เร่งสี %u\n", Display::vivid()); }
    else if (lower == "autoloc") { autoLocation(true); Serial.println("[Geo] ตำแหน่ง: " + settings.addrTh); }
    else if (lower == "sound") Sound::runTest();
    else if (lower == "chime") { Sound::play(Sound::MEL_RAIN); Serial.println("[Sound] " + Sound::status()); }
    else if (lower == "update") { if (!AutoUpdate::checkNow(true)) Serial.println(F("[Update] ไม่มีการอัปเดต")); }
    else if (lower == "status") {
      Serial.printf("เวอร์ชัน %s | โหมด %s | IP %s | RSSI %d dBm | RAM ว่าง %u bytes | ทำงานมา %lu วินาที\n",
                    APP_VERSION, portalMode ? "ตั้งค่า" : "ปกติ",
                    (portalMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(), WiFi.RSSI(),
                    ESP.getFreeHeap(), millis() / 1000);
      Serial.printf("โหมดภาพ %u | เสียง: %s\n", Display::preset() + 1, Sound::status().c_str());
      Serial.printf("RAM ภายในต่ำสุดที่เคยเหลือ %u bytes | PSRAM ว่าง %u KB | รีสตาร์ทครั้งก่อน: %s\n", ESP.getMinFreeHeap(),
                    ESP.getFreePsram() / 1024, lastResetInfo.length() ? lastResetInfo.c_str() : "ปกติ");
    } else if (lower.startsWith("city ")) {
      if (portalMode) { Serial.println(F("ต้องเชื่อมต่อ Wi-Fi ก่อน")); return; }
      String msg;
      changeCity(cmd.substring(5), msg);
      if (!msg.isEmpty()) Serial.println(msg);
    } else {
      Serial.println("ไม่รู้จักคำสั่ง \"" + cmd + "\" พิมพ์ help เพื่อดูคำสั่ง");
    }
  }
}

static void handleSerial() {
  while (Serial.available()) {
    uint8_t b = Serial.read();
    if (!Improv::feed(b, processChar)) processChar((char)b);
  }
}

static void handleButton() {
  if (digitalRead(PIN_BUTTON) == LOW) {
    if (buttonDownAt == 0) buttonDownAt = millis();
    if (millis() - buttonDownAt > 3000) resetWifiAndRestart();
  } else {
    buttonDownAt = 0;
  }
}

// เตือนด้วยเสียงเมื่อเรดาร์เห็นฝนกำลังเข้ามา/เริ่มตกบริเวณนี้ (ไม่เกินชั่วโมงละครั้ง ไม่เตือนกลางคืน)
static void radarAlert() {
  static uint32_t seen = 0;
  static unsigned long lastAlert = 0;
  static bool wasRaining = false, wasComing = false;
  RadarInfo r = Radar::info();
  if (!r.ready || !r.covered || r.frames == 0 || r.frameTime[r.frames - 1] == seen) return;
  seen = r.frameTime[r.frames - 1];
  bool raining = r.hereMmh >= 0.5f;
  bool coming = r.approaching && r.etaMin > 0 && r.etaMin <= 45;
  bool trigger = (raining && !wasRaining) || (coming && !wasComing);
  wasRaining = raining;
  wasComing = coming;
  struct tm t;
  bool night = getLocalTime(&t, 0) && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR);
  if (trigger && !night && (lastAlert == 0 || millis() - lastAlert > 3600000UL)) {
    lastAlert = millis();
    Serial.println(F("[Radar] เตือน: มีฝนกำลังเข้ามา/เริ่มตกบริเวณนี้"));
    Sound::play(Sound::MEL_RAIN);
  }
}

// ---------------- setup / loop ----------------
// ---------------- บันทึกสาเหตุการรีสตาร์ท ----------------
RTC_NOINIT_ATTR static uint32_t crashStreak;  // จำนวนครั้งที่ค้าง/ไฟตกติดกัน (ไม่หายตอนรีสตาร์ท)
static bool safeMode = false;

static void checkResetReason() {
  esp_reset_reason_t rr = esp_reset_reason();
  const char* name = rr == ESP_RST_PANIC ? "PANIC" : rr == ESP_RST_INT_WDT ? "INT_WDT" : rr == ESP_RST_TASK_WDT ? "TASK_WDT"
                   : rr == ESP_RST_WDT ? "WDT" : rr == ESP_RST_BROWNOUT ? "BROWNOUT" : rr == ESP_RST_POWERON ? "POWERON"
                   : rr == ESP_RST_SW ? "SW" : rr == ESP_RST_USB ? "USB" : "OTHER";
  bool crash = rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT;
  if (rr == ESP_RST_POWERON || crashStreak > 100) crashStreak = 0;
  if (crash) {
    crashStreak++;
    lastResetInfo = String(name) + " @" + stageName(g_stage) + " p" + String(g_aux) + " / " + stageName(g_radarStage);
    Preferences p;
    p.begin("diag", false);
    p.putString("last", lastResetInfo);
    p.putUInt("count", p.getUInt("count", 0) + 1);
    p.end();
    Serial.printf("\n⚠️  รีสตาร์ทครั้งก่อนเพราะ %s (ครั้งที่ %u ติดกัน)\n", lastResetInfo.c_str(), crashStreak);
    if (rr == ESP_RST_BROWNOUT) Serial.println(F("   ไฟตก: ลองเปลี่ยนสาย USB / ที่ชาร์จ (แนะนำ 5V 1A ขึ้นไป)"));
  } else {
    crashStreak = 0;
    Preferences p;
    p.begin("diag", true);
    lastResetInfo = p.getString("last", "");
    p.end();
  }
  safeMode = crashStreak >= 2;  // ค้างซ้ำ 2 ครั้ง → ปิดเรดาร์/หาตำแหน่ง/เสียงชั่วคราว ให้ส่วนอื่นใช้งานได้
  lowPower = rr == ESP_RST_BROWNOUT;
  if (safeMode) Serial.println(F("[Safe] ค้างซ้ำหลายครั้ง — ปิดเรดาร์ไว้ก่อนในรอบนี้"));
  g_stage = ST_BOOT;
  g_radarStage = 0;
  g_aux = 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  checkResetReason();
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  Serial.printf("\n\n🌤️  %s v%s กำลังเริ่มทำงาน...\n", APP_NAME, APP_VERSION);

  Store::load(settings);
  // 1.3.3: เวอร์ชันเก่าใช้ "แตะค้าง" เพื่อหมุนจอ ทำให้บางเครื่องค้างอยู่ในทิศกลับหัว → คืนค่าทิศจอที่ถูกต้องครั้งเดียว
  {
    Preferences p;
    p.begin("s3ui", false);
    if (!p.getBool("rotfix", false)) {
      if (settings.displayMode != DEFAULT_DISPLAY_MODE) {
        Serial.printf("[Display] คืนค่าทิศจอจากโหมด %u เป็นค่าเริ่มต้น\n", settings.displayMode + 1);
        settings.displayMode = DEFAULT_DISPLAY_MODE;
        Store::saveDisplayMode(DEFAULT_DISPLAY_MODE);
      }
      p.putBool("rotfix", true);
    }
    p.end();
  }
  Display::begin(settings.displayMode);
  Display::setIdleHook([]() { Web::loop(); handleSerial(); });
  Serial.printf("[Display] โหมด %u: %s (พิมพ์ display เพื่อเปลี่ยน)\n", settings.displayMode + 1,
                Display::modeName(settings.displayMode));
  Display::showBoot(APP_VERSION);
  if (crashStreak > 0) {  // แสดงสาเหตุบนจอค้างไว้ 25 วินาที หรือจนกว่าจะแตะจอ (ถ่ายรูปส่งให้ผู้พัฒนาได้)
    Display::showBootReason(lastResetInfo, crashStreak, safeMode);
    Display::waitTap(25000);
  }
  // ช่วงหน้าจอเริ่มต้น: แตะค้างเพื่อเปลี่ยนโหมดจอได้ แม้ยังไม่ได้ตั้งค่า Wi-Fi
  for (unsigned long t = millis(); millis() - t < 2500;) {
    if (Display::loop() == Display::Touch::LongPress) Display::choosePreset();
    delay(10);
  }
  // ครั้งแรกหลังอัปเดต: ให้ผู้ใช้เลือกโหมดภาพที่สว่างที่สุด แล้วทดสอบลำโพง
  if (Display::needsPresetChoice()) Display::choosePreset();
  if (!safeMode && Sound::begin()) {
    if (Sound::needsTest()) Sound::runTest();
    else if (!lowPower) Sound::play(Sound::MEL_BOOT);
  }
  if (!connectWifi()) {
    startPortal();
    return;
  }
  // ตรวจเวอร์ชันใหม่ทันทีหลังต่อ Wi-Fi (ก่อนเริ่มงานอื่น) — ถ้าเวอร์ชันนี้มีปัญหาจนรีสตาร์ทเร็ว ก็ยังอัปเดตตัวแก้ได้
  g_stage = ST_UPDATE;
  AutoUpdate::checkNow(true);
  g_stage = ST_BOOT;

  // เมืองที่กรอกจากหน้าตั้งค่า ยังไม่มีพิกัด -> ค้นหาตอนนี้
  if (isnan(settings.lat) || isnan(settings.lon)) {
    String msg;
    if (!changeCity(settings.city, msg)) {
      Serial.println("[City] " + msg + " — ใช้ค่าเริ่มต้น " DEFAULT_CITY);
      settings.city = DEFAULT_CITY;
      settings.lat = DEFAULT_LAT;
      settings.lon = DEFAULT_LON;
      Store::saveLocation(settings.city, settings.lat, settings.lon);
    }
  }
  if (!safeMode) autoLocation(false);  // หาตำแหน่งจากอินเทอร์เน็ต (ถ้าไม่ได้ตั้งเอง) + ชื่อตำบล/อำเภอ/จังหวัด
  Web::beginDashboard();
  Radar::setLocation(settings.lat, settings.lon);
  if (!safeMode) Radar::begin();
  Display::showLoading();
  printHelp();
  requestWeatherRefresh();
}

void loop() {
  g_stage = ST_LOOP;
  if (crashStreak && millis() > 600000UL) crashStreak = 0;  // ทำงานได้ 10 นาที = ปกติแล้ว
  if (safeMode && millis() > 600000UL && WiFi.status() == WL_CONNECTED) {  // ออกจากโหมดปลอดภัย: เปิดเรดาร์/หาตำแหน่ง
    safeMode = false;
    Serial.println(F("[Safe] ทำงานปกติครบ 10 นาที — เปิดเรดาร์และหาตำแหน่ง"));
    autoLocation(false);
    Radar::setLocation(settings.lat, settings.lon);
    Radar::begin();
    if (!Sound::ready() && Sound::begin()) Sound::play(Sound::MEL_OK);
  }
  Web::loop();
  handleSerial();
  handleButton();

  g_stage = ST_RENDER;
  Display::Touch touch = Display::loop();
  g_stage = ST_LOOP;
  if (touch == Display::Touch::LongPress) Display::choosePreset();
  Sound::loop();

  if (portalMode) {
    // ตั้งค่า Wi-Fi บนจอสัมผัส
    String ssid, pass;
    if (Display::wifiSetupLoop(ssid, pass)) {
      Display::showConnecting(ssid);
      WiFi.begin(ssid.c_str(), pass.c_str());
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { Web::loop(); delay(100); }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] ตั้งค่าบนจอสำเร็จ: %s\n", ssid.c_str());
        Store::saveWifi(ssid, pass);
        delay(800);
        ESP.restart();
      }
      Serial.printf("[WiFi] ตั้งค่าบนจอไม่สำเร็จ สถานะ %d\n", WiFi.status());
      Display::showWifiFailedReason(WiFi.status());
      WiFi.disconnect(false);
      delay(3500);
      Display::wifiSetupBackToKeyboard();
    }
    // มีรหัส Wi-Fi เดิมอยู่แล้ว (เช่น เราเตอร์ดับชั่วคราว) -> รอสักพักแล้วลองต่อใหม่ (ยกเว้นกำลังพิมพ์อยู่)
    if (!settings.wifiSsid.isEmpty() && millis() - portalStartedAt > PORTAL_RETRY_MS && !Display::wifiSetupBusy()) ESP.restart();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 10000) {
      lastTry = millis();
      Serial.println(F("[WiFi] หลุดการเชื่อมต่อ กำลังต่อใหม่..."));
      WiFi.reconnect();
    }
    return;
  }

  if (touch == Display::Touch::Tap) {
    Display::nextPage();
    Sound::play(Sound::MEL_TICK);  // เสียงคลิกเบา ๆ ยืนยันการแตะ
  }
  g_stage = ST_UPDATE;
  AutoUpdate::loop();
  g_stage = ST_LOOP;
  radarAlert();

  // ข้อมูลตลาด (ทอง/หุ้น) แยกรอบจากข้อมูลอากาศ
  if (weather.fetchedAt != 0 && (long)(millis() - nextMarketAt) >= 0) {
    g_stage = ST_MARKET;
    bool mok = MarketApi::fetch(market);
    g_stage = ST_LOOP;
    if (mok) {
      Serial.printf("[Market] SET %.2f (%+.2f%%) | ทองแท่งขาย %.0f บาท | USD %.2f\n", market.setPrice,
                    market.setPct, market.barSell, market.usdthb);
      Display::refresh();
    } else {
      Serial.println("[Market] ดึงข้อมูลไม่สำเร็จ: " + market.error);
    }
    nextMarketAt = millis() + (mok ? MARKET_UPDATE_MS : WEATHER_RETRY_MS);
  }

  if ((long)(millis() - nextWeatherAt) >= 0) {
    Display::showUpdating();
    Display::setRgb(false, true, false);  // เขียว = กำลังดึงข้อมูล
    g_stage = ST_WEATHER;
    bool ok = WeatherApi::fetch(settings.lat, settings.lon, weather);
    g_stage = ST_AIR;
    if (ok && !WeatherApi::fetchAir(settings.lat, settings.lon, air))
      Serial.println("[Air] ดึงค่าฝุ่นไม่สำเร็จ: " + air.error);
    Display::setRgb(!ok, false, false);   // แดงค้าง = ดึงข้อมูลไม่สำเร็จ
    if (ok) {
      static long tzSet = LONG_MIN;
      if (tzSet != weather.utcOffset) {  // ตั้งนาฬิกาตามเขตเวลาของเมือง
        configTime(weather.utcOffset, 0, "pool.ntp.org", "time.google.com");
        tzSet = weather.utcOffset;
      }
    } else {
      Serial.println("[Weather] ดึงข้อมูลไม่สำเร็จ: " + weather.error);
    }
    Display::showWeather(settings, weather, air, market);
    // เตือนฝนด้วยเสียง: ครั้งเดียวต่อช่วงฝน และไม่ส่งเสียงตอนกลางคืน
    static bool rainAlerted = false;
    if (ok && weather.rainNext2h >= RAIN_ALERT_PERCENT) {
      struct tm t;
      bool night = getLocalTime(&t, 0) && (t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR);
      if (!rainAlerted && !night) Sound::play(Sound::MEL_RAIN);
      rainAlerted = true;
    } else if (ok && weather.rainNext2h >= 0 && weather.rainNext2h < RAIN_ALERT_PERCENT - 20) {
      rainAlerted = false;
    }
    printWeatherReport();
    nextWeatherAt = millis() + (ok ? WEATHER_UPDATE_MS : WEATHER_RETRY_MS);
  }
}
