// เรดาร์ฝน: RainViewer (ไทล์ z7 ขนาด 512 px = มาตราส่วนเท่า z8) + แผนที่ Esri World Dark Gray z8
// ประกอบเป็นภาพเต็มจอ 320x240 ไว้ใน PSRAM เฟรมละภาพ แล้วจอแค่คัดลอกไปแสดง
#include "radar.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"
#include "net.h"

namespace {

constexpr int Z = 8;                 // มาตราส่วนของภาพบนจอ (เทียบเท่า zoom 8)
constexpr double WORLD = 256.0 * (1 << Z);

// สีชุด Universal Blue ของ RainViewer: dBZ 15..65 (ทึบ)
const uint8_t PAL[][3] = {
    {0x88, 0xdd, 0xee}, {0x6c, 0xd1, 0xeb}, {0x51, 0xc5, 0xe8}, {0x36, 0xba, 0xe5}, {0x1b, 0xae, 0xe2}, {0x00, 0xa3, 0xe0},
    {0x00, 0x9a, 0xd5}, {0x00, 0x91, 0xca}, {0x00, 0x88, 0xbf}, {0x00, 0x7f, 0xb4}, {0x00, 0x77, 0xaa}, {0x00, 0x70, 0xa3},
    {0x00, 0x69, 0x9c}, {0x00, 0x62, 0x95}, {0x00, 0x5b, 0x8e}, {0x00, 0x55, 0x88}, {0x00, 0x51, 0x80}, {0x00, 0x4e, 0x78},
    {0x00, 0x4a, 0x70}, {0x00, 0x47, 0x68}, {0xff, 0xee, 0x00}, {0xff, 0xe0, 0x00}, {0xff, 0xd2, 0x00}, {0xff, 0xc5, 0x00},
    {0xff, 0xb7, 0x00}, {0xff, 0xaa, 0x00}, {0xff, 0x9f, 0x00}, {0xff, 0x95, 0x00}, {0xff, 0x8b, 0x00}, {0xff, 0x81, 0x00},
    {0xff, 0x44, 0x00}, {0xf2, 0x36, 0x00}, {0xe6, 0x28, 0x00}, {0xd9, 0x1b, 0x00}, {0xcd, 0x0d, 0x00}, {0xc1, 0x00, 0x00},
    {0xa8, 0x00, 0x00}, {0x8f, 0x00, 0x00}, {0x76, 0x00, 0x00}, {0x5d, 0x00, 0x00}, {0xff, 0xaa, 0xff}, {0xff, 0x9f, 0xff},
    {0xff, 0x95, 0xff}, {0xff, 0x8b, 0xff}, {0xff, 0x81, 0xff}, {0xff, 0x77, 0xff}, {0xff, 0x6c, 0xff}, {0xff, 0x62, 0xff},
    {0xff, 0x58, 0xff}, {0xff, 0x4e, 0xff}, {0xff, 0xff, 0xff}};
constexpr int PAL_N = sizeof(PAL) / sizeof(PAL[0]);
constexpr int PAL_DBZ0 = 15;

// แปลงพิกเซล sprite (RGB565 สลับไบต์) → dBZ (0 = ไม่มีฝน/ฝนบางมาก)
int toDbz(uint16_t raw) {
  if (!raw) return 0;
  uint16_t c = (uint16_t)((raw & 0xFF) << 8 | raw >> 8);
  int r = (c >> 8) & 0xF8, g = (c >> 3) & 0xFC, b = (c << 3) & 0xF8;
  int best = -1, bd = 1 << 30;
  for (int i = 0; i < PAL_N; i++) {
    int dr = r - PAL[i][0], dg = g - PAL[i][1], db = b - PAL[i][2];
    int d = dr * dr + dg * dg + db * db;
    if (d < bd) { bd = d; best = i; }
  }
  if (bd > 40 * 40 * 3) return 0;  // สีโปร่ง (ฝนบางมาก) ที่ถูกผสมกับพื้นดำ
  return PAL_DBZ0 + best;
}
float dbzToMmh(int dbz) { return dbz <= 0 ? 0 : powf(powf(10, dbz / 10.0f) / 200.0f, 0.625f); }

struct Frame {
  uint32_t ts = 0;
  LGFX_Sprite* img = nullptr;
  float nearestKm = NAN;
  int bearing = -1;
  float hereMmh = 0, maxMmh = 0;
};

SemaphoreHandle_t mtx;
Frame frames[RADAR_FRAMES];  // เก่า → ใหม่ (ช่องว่างอยู่ต้นอาร์เรย์)
RadarInfo inf;
LGFX_Sprite *base = nullptr, *labels = nullptr, *layer = nullptr;
volatile double wantLat = NAN, wantLon = NAN;
double curLat = NAN, curLon = NAN;
double originX = 0, originY = 0;  // พิกัดโลก (Z) ของมุมซ้ายบนของจอ
volatile bool locChanged = false;

LGFX_Sprite* newSprite() {
  auto* s = new LGFX_Sprite();
  s->setColorDepth(16);
  s->setPsram(true);
  if (!s->createSprite(RADAR_W, RADAR_H)) { delete s; return nullptr; }
  return s;
}

// ---------- HTTP ----------
WiFiClientSecure tls;
HTTPClient http;
uint8_t* dl = nullptr;
size_t dlCap = 0;

void setError(const String& e) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  inf.error = e;
  xSemaphoreGive(mtx);
}

int fetch(const String& url, size_t& len) {
  len = 0;
  if (ESP.getFreeHeap() < 60000) return -4;  // หน่วยความจำภายในเหลือน้อย ข้ามรอบนี้
  Net::Guard ng;
  g_radarStage = ST_RADAR_NET;
  http.setReuse(true);
  http.setTimeout(15000);
  http.setUserAgent("esp32-weather-s3/" APP_VERSION);
  if (!http.begin(tls, url)) return -1;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return code; }
  int size = http.getSize();
  WiFiClient* s = http.getStreamPtr();
  unsigned long t0 = millis();
  while ((size < 0 || len < (size_t)size) && millis() - t0 < 20000 && (http.connected() || s->available())) {
    int n = s->available();
    if (n <= 0) { delay(2); continue; }
    if (len + n > dlCap) {
      size_t nc = max(dlCap * 2, len + n + 16384);
      uint8_t* p = (uint8_t*)ps_realloc(dl, nc);
      if (!p) { http.end(); return -2; }
      dl = p;
      dlCap = nc;
    }
    len += s->readBytes(dl + len, n);
  }
  http.end();
  g_radarStage = ST_RADAR_IDLE;
  return (size < 0 || len == (size_t)size) ? 200 : -3;
}

void worldXY(double lat, double lon, double& x, double& y) {
  x = (lon + 180.0) / 360.0 * WORLD;
  double s = sin(lat * DEG_TO_RAD);
  y = (0.5 - log((1 + s) / (1 - s)) / (4 * PI)) * WORLD;
}

// ---------- แผนที่พื้นหลัง (โหลดครั้งเดียวต่อพิกัด) ----------
bool loadBase() {
  if (!base) base = newSprite();
  if (!labels) labels = newSprite();
  if (!layer) layer = newSprite();
  if (!base || !labels || !layer) { setError("PSRAM"); return false; }
  base->fillScreen(base->color888(28, 30, 36));
  labels->fillScreen(0);
  int tx0 = (int)floor(originX / 256), ty0 = (int)floor(originY / 256);
  int tx1 = (int)floor((originX + RADAR_W - 1) / 256), ty1 = (int)floor((originY + RADAR_H - 1) / 256);
  bool ok = true;
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      int dx = tx * 256 - (int)originX, dy = ty * 256 - (int)originY;
      size_t len;
      String t = String(Z) + "/" + ty + "/" + tx;
      if (fetch("https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/" + t, len) == 200) {
        g_radarStage = ST_RADAR_PNG;
        if (dl[0] == 0xFF) base->drawJpg(dl, len, dx, dy);
        else base->drawPng(dl, len, dx, dy);
        g_radarStage = ST_RADAR_IDLE;
        vTaskDelay(1);
      } else ok = false;
      if (fetch("https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Reference/MapServer/tile/" + t, len) == 200)
      { g_radarStage = ST_RADAR_PNG; labels->drawPng(dl, len, dx, dy); g_radarStage = ST_RADAR_IDLE; vTaskDelay(1); }
    }
  // ทำให้แผนที่สว่างขึ้นเล็กน้อย (แผนที่ Esri เข้มมากบนจอเล็ก)
  uint16_t* p = (uint16_t*)base->getBuffer();
  for (int i = 0; i < RADAR_W * RADAR_H; i++) {
    uint16_t c = (uint16_t)((p[i] & 0xFF) << 8 | p[i] >> 8);
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = min(31, r * 5 / 4 + 1); g = min(63, g * 5 / 4 + 2); b = min(31, b * 5 / 4 + 2);
    c = r << 11 | g << 5 | b;
    p[i] = (uint16_t)((c & 0xFF) << 8 | c >> 8);
  }
  return ok;
}

bool checkCoverage(const String& host) {
  int tx = (int)floor((originX + RADAR_W / 2) / 512), ty = (int)floor((originY + RADAR_H / 2) / 512);
  size_t len;
  if (fetch(host + "/v2/coverage/0/512/7/" + tx + "/" + ty + "/0/0_0.png", len) != 200) return true;
  const uint32_t mark = layer->color888(123, 45, 67);
  layer->fillScreen(mark);
  int px = (int)(originX + RADAR_W / 2) - tx * 512, py = (int)(originY + RADAR_H / 2) - ty * 512;
  // วาดไทล์ให้ตำแหน่งของเราอยู่ที่ (0,0) ของ layer แล้วอ่านพิกเซล: โปร่งใส (สีไม่เปลี่ยน) = ครอบคลุม
  layer->drawPng(dl, len, 0, 0, 4, 4, px, py);
  return layer->readPixel(1, 1) == layer->readPixel(10, 10);
}

// ---------- ประกอบ 1 เฟรม ----------
bool buildFrame(const String& host, const String& path, Frame& f) {
  layer->fillScreen(0);
  int tx0 = (int)floor(originX / 512), ty0 = (int)floor(originY / 512);
  int tx1 = (int)floor((originX + RADAR_W - 1) / 512), ty1 = (int)floor((originY + RADAR_H - 1) / 512);
  for (int ty = ty0; ty <= ty1; ty++)
    for (int tx = tx0; tx <= tx1; tx++) {
      size_t len;
      int code = fetch(host + path + "/512/7/" + tx + "/" + ty + "/2/1_0.png", len);
      if (code != 200) { setError("radar HTTP " + String(code)); return false; }
      int dx = tx * 512 - (int)originX, dy = ty * 512 - (int)originY;
      int ox = dx < 0 ? -dx : 0, oy = dy < 0 ? -dy : 0;
      g_radarStage = ST_RADAR_PNG;
      layer->drawPng(dl, len, max(dx, 0), max(dy, 0), RADAR_W - max(dx, 0), RADAR_H - max(dy, 0), ox, oy);
      g_radarStage = ST_RADAR_IDLE;
      vTaskDelay(1);
    }
  LGFX_Sprite* img = newSprite();
  if (!img) { setError("PSRAM"); return false; }
  // วิเคราะห์ + ผสมสีฝนลงบนแผนที่
  const uint16_t* L = (const uint16_t*)layer->getBuffer();
  const uint16_t* B = (const uint16_t*)base->getBuffer();
  const uint16_t* T = (const uint16_t*)labels->getBuffer();
  uint16_t* O = (uint16_t*)img->getBuffer();
  const int cx = RADAR_W / 2, cy = RADAR_H / 2;
  const float kmpp = inf.kmPerPx;
  float best = 1e9;
  int bestX = 0, bestY = 0;
  float here = 0, mx = 0;
  int lastRaw = -1, lastDbz = 0;
  g_radarStage = ST_RADAR_MIX;
  for (int y = 0; y < RADAR_H; y++) {
    if ((y & 31) == 0) vTaskDelay(1);  // ปล่อยให้งานอื่นทำงาน (กัน watchdog)
    for (int x = 0; x < RADAR_W; x++) {
      int i = y * RADAR_W + x;
      uint16_t l = L[i], o = B[i];
      if (l) {
        int dbz = l == lastRaw ? lastDbz : toDbz(l);
        lastRaw = l;
        lastDbz = dbz;
        // ผสม 80% สีฝน + 20% แผนที่
        uint16_t a = (uint16_t)((l & 0xFF) << 8 | l >> 8), b = (uint16_t)((o & 0xFF) << 8 | o >> 8);
        int r = (((a >> 11) & 31) * 4 + ((b >> 11) & 31)) / 5, g = (((a >> 5) & 63) * 4 + ((b >> 5) & 63)) / 5,
            bl = ((a & 31) * 4 + (b & 31)) / 5;
        uint16_t c = r << 11 | g << 5 | bl;
        o = (uint16_t)((c & 0xFF) << 8 | c >> 8);
        if (dbz >= 18) {
          int dx = x - cx, dy = y - cy;
          float d2 = (float)(dx * dx + dy * dy);
          float km = sqrtf(d2) * kmpp;
          float mmh = dbzToMmh(dbz);
          if (km <= 2.5f) here = max(here, mmh);
          if (km <= 30) mx = max(mx, mmh);
          if (km <= 80 && d2 < best) { best = d2; bestX = dx; bestY = dy; }
        }
      }
      if (T[i]) o = T[i];  // ชื่อสถานที่ทับด้านบน
      O[i] = o;
    }
  }
  g_radarStage = ST_RADAR_IDLE;
  f.img = img;
  f.hereMmh = here;
  f.maxMmh = mx;
  if (best < 1e9) {
    f.nearestKm = sqrtf(best) * kmpp;
    int brg = (int)lroundf(atan2f((float)bestX, (float)-bestY) * RAD_TO_DEG);
    f.bearing = (brg + 360) % 360;
  } else {
    f.nearestKm = NAN;
    f.bearing = -1;
  }
  return true;
}

void setPhase(uint8_t ph, uint8_t prog) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  inf.phase = ph;
  inf.progress = prog;
  xSemaphoreGive(mtx);
}

void analyze() {
  // ใช้ระยะกลุ่มฝนที่ใกล้สุดของ 3 เฟรมล่าสุดหาความเร็วที่เข้าหา
  inf.approaching = false;
  inf.speedKmh = NAN;
  inf.etaMin = -1;
  const Frame& last = frames[RADAR_FRAMES - 1];
  inf.hereMmh = last.hereMmh;
  inf.maxMmh = last.maxMmh;
  inf.nearestKm = last.nearestKm;
  inf.bearing = last.bearing;
  const Frame& prev = frames[RADAR_FRAMES - 3];
  if (!last.img || !prev.img || isnan(last.nearestKm) || isnan(prev.nearestKm) || last.ts <= prev.ts) return;
  const Frame& mid = frames[RADAR_FRAMES - 2];
  bool monotonic = mid.img && !isnan(mid.nearestKm) && mid.nearestKm <= prev.nearestKm + 1 && last.nearestKm <= mid.nearestKm + 1;
  float dKm = prev.nearestKm - last.nearestKm;
  float hours = (last.ts - prev.ts) / 3600.0f;
  if (monotonic && dKm > 2 && last.nearestKm > 2.5f) {
    inf.speedKmh = dKm / hours;
    int eta = (int)lroundf(last.nearestKm / inf.speedKmh * 60);
    if (eta <= 180) { inf.approaching = true; inf.etaMin = eta; }
  }
}

void update() {
  size_t len;
  if (fetch("https://api.rainviewer.com/public/weather-maps.json", len) != 200) { setError("meta"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)dl, len)) { setError("meta json"); return; }
  String host = doc["host"] | "https://tilecache.rainviewer.com";
  JsonArrayConst past = doc["radar"]["past"];
  int n = past.size();
  if (n == 0) return;
  if (locChanged || isnan(curLat)) {
    locChanged = false;
    curLat = wantLat;
    curLon = wantLon;
    double wx, wy;
    worldXY(curLat, curLon, wx, wy);
    originX = floor(wx) - RADAR_W / 2;
    originY = floor(wy) - RADAR_H / 2;
    float kmpp = 40075.0f * cosf(curLat * DEG_TO_RAD) / WORLD;
    xSemaphoreTake(mtx, portMAX_DELAY);
    inf.kmPerPx = kmpp;
    xSemaphoreGive(mtx);
    setPhase(1, 0);
    loadBase();
    bool cov = checkCoverage(host);
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (auto& f : frames) { if (f.img) { f.img->deleteSprite(); delete f.img; } f = Frame(); }
    inf.covered = cov;
    inf.frames = 0;
    xSemaphoreGive(mtx);
    Serial.printf("[Radar] ตำแหน่ง %.4f, %.4f  %.2f กม./พิกเซล  เรดาร์ครอบคลุม: %s\n", curLat, curLon, inf.kmPerPx, cov ? "ใช่" : "ไม่");
  }
  // เฟรมที่ต้องการ = 6 เฟรมล่าสุด
  for (int k = max(0, n - RADAR_FRAMES); k < n; k++) {
    uint32_t ts = past[k]["time"] | 0;
    String path = past[k]["path"] | "";
    bool have = false;
    for (auto& f : frames) if (f.ts == ts && f.img) have = true;
    if (have || path.isEmpty()) continue;
    setPhase(2, k - max(0, n - RADAR_FRAMES) + 1);
    Frame nf;
    nf.ts = ts;
    if (!buildFrame(host, path, nf)) return;
    xSemaphoreTake(mtx, portMAX_DELAY);
    // เลื่อนเฟรมเก่าออก แล้วใส่เฟรมใหม่ท้ายสุด
    if (frames[0].img) { frames[0].img->deleteSprite(); delete frames[0].img; }
    for (int i = 0; i < RADAR_FRAMES - 1; i++) frames[i] = frames[i + 1];
    frames[RADAR_FRAMES - 1] = nf;
    inf.frames = 0;
    for (int i = 0; i < RADAR_FRAMES; i++)
      if (frames[i].img) inf.frameTime[inf.frames++] = frames[i].ts;
    analyze();
    inf.ready = inf.frames > 0;
    inf.updatedAt = time(nullptr);
    inf.error = "";
    xSemaphoreGive(mtx);
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  inf.updatedAt = time(nullptr);
  inf.phase = 0;
  xSemaphoreGive(mtx);
  RadarInfo i = Radar::info();
  Serial.printf("[Radar] %d เฟรม | ตรงนี้ %.1f มม./ชม. | ฝนใกล้สุด %.1f กม. ทิศ %d° | %s\n", i.frames, i.hereMmh, i.nearestKm,
                i.bearing, i.approaching ? ("กำลังเข้ามา ~" + String(i.etaMin) + " นาที").c_str() : "-");
}

void task(void*) {
  tls.setInsecure();
  vTaskDelay(pdMS_TO_TICKS(6000));
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !isnan(wantLat)) update();
    for (int i = 0; i < (int)(RADAR_UPDATE_MS / 1000) && !locChanged; i++) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

}  // namespace

namespace Radar {

bool isStarted = false;
bool started() { return isStarted; }

void begin() {
  if (isStarted) return;
  isStarted = true;
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "radar", 20480, nullptr, 1, nullptr, 0);
}

void setLocation(double lat, double lon) {
  if (isnan(lat) || isnan(lon)) return;
  if (fabs(lat - wantLat) < 1e-4 && fabs(lon - wantLon) < 1e-4) return;
  wantLat = lat;
  wantLon = lon;
  locChanged = true;
}

RadarInfo info() {
  if (!mtx) return RadarInfo();
  xSemaphoreTake(mtx, portMAX_DELAY);
  RadarInfo r = inf;
  xSemaphoreGive(mtx);
  return r;
}

bool drawFrame(int i, lgfx::LovyanGFX* dst) {
  if (!mtx) return false;
  xSemaphoreTake(mtx, portMAX_DELAY);
  // i นับเฉพาะเฟรมที่มีภาพ
  int k = -1;
  for (int j = 0; j < RADAR_FRAMES; j++)
    if (frames[j].img && ++k == i) {
      frames[j].img->pushSprite(dst, 0, 0);
      xSemaphoreGive(mtx);
      return true;
    }
  xSemaphoreGive(mtx);
  return false;
}

}  // namespace Radar
