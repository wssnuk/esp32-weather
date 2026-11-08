#include "radar.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"

namespace {

RadarData slots[2];
volatile int active = -1;  // ช่องที่จอใช้อยู่ (task เขียนอีกช่องแล้วค่อยสลับ)

bool httpGet(const String& url, uint8_t** buf, size_t* len, String* text) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[Radar] HTTP %d %s\n", code, url.c_str());
    http.end();
    return false;
  }
  if (text) {
    *text = http.getString();
    http.end();
    return text->length() > 0;
  }
  int size = http.getSize();
  size_t cap = size > 0 ? size : 200 * 1024;
  uint8_t* p = (uint8_t*)ps_malloc(cap);
  if (!p) { http.end(); return false; }
  WiFiClient* s = http.getStreamPtr();
  size_t got = 0;
  unsigned long t0 = millis();
  while (got < cap && millis() - t0 < 15000 && (http.connected() || s->available())) {
    int n = s->available();
    if (n > 0) got += s->readBytes(p + got, min((size_t)n, cap - got));
    else delay(5);
    if (size > 0 && got >= (size_t)size) break;
  }
  http.end();
  if (got == 0 || (size > 0 && got != (size_t)size)) { free(p); return false; }
  *buf = p;
  *len = got;
  return true;
}

float fnum(JsonVariantConst v) { return v.isNull() ? NAN : v.as<float>(); }

bool fetchOnce() {
  String body;
  if (!httpGet(RADAR_DATA_URL "radar.json", nullptr, nullptr, &body)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  if (!(doc["ok"] | false)) return false;
  uint32_t gen = doc["generated"] | 0;
  int cur = active;
  if (cur >= 0 && slots[cur].generated == gen) {  // ยังเป็นชุดเดิม
    slots[cur].fetchedMs = millis();
    return true;
  }
  int nx = cur == 0 ? 1 : 0;
  RadarData& d = slots[nx];
  for (int i = 0; i < 4; i++) { free(d.jpg[i]); d.jpg[i] = nullptr; d.len[i] = 0; }
  d = RadarData();
  d.rainingHere = doc["raining_here"] | false;
  d.approaching = doc["approaching"] | false;
  d.nearestKm = fnum(doc["nearest_km"]);
  d.speedKmh = fnum(doc["speed_kmh"]);
  d.bearing = doc["bearing"].isNull() ? -1 : doc["bearing"].as<int>();
  d.etaMin = doc["eta_min"].isNull() ? -1 : doc["eta_min"].as<int>();
  d.model = String((const char*)(doc["source"] | "model")) != "rainviewer";
  d.generated = gen;
  d.lat = doc["lat"] | NAN;
  d.lon = doc["lon"] | NAN;
  d.kmPerPx = doc["km_per_px"] | 0.45f;
  d.w = doc["w"] | 224;
  d.h = doc["h"] | 204;
  JsonArrayConst fr = doc["frames"];
  int n = 0;
  for (JsonVariantConst v : fr) if (n < 4) d.frames[n++] = v.as<uint32_t>();
  if (n != 4) return false;
  for (int i = 0; i < 4; i++) {
    String url = String(RADAR_DATA_URL "f") + i + ".jpg?g=" + gen;
    if (!httpGet(url, &d.jpg[i], &d.len[i], nullptr)) {
      Serial.printf("[Radar] โหลดภาพ f%d ไม่สำเร็จ\n", i);
      return false;
    }
  }
  d.valid = true;
  d.fetchedMs = millis();
  active = nx;  // สลับให้จอใช้ชุดใหม่ (ชุดเก่าจะถูกล้างในรอบถัดไป ~10 นาที)
  Serial.printf("[Radar] อัปเดตแผนที่ฝน: %s ฝนใกล้สุด %.1f กม. ทิศ %d° %s\n", d.model ? "แบบจำลอง" : "เรดาร์",
                d.nearestKm, d.bearing, d.approaching ? "(กำลังเข้ามา)" : "");
  return true;
}

void task(void*) {
  vTaskDelay(pdMS_TO_TICKS(8000));  // ให้ข้อมูลอากาศโหลดก่อน
  for (;;) {
    bool ok = false;
    if (WiFi.status() == WL_CONNECTED) ok = fetchOnce();
    vTaskDelay(pdMS_TO_TICKS(ok ? RADAR_UPDATE_MS : 60000));
  }
}

}  // namespace

namespace Radar {

void begin() {
  static bool started = false;
  if (started) return;
  started = true;
  xTaskCreatePinnedToCore(task, "radar", 12288, nullptr, 1, nullptr, 0);
}

const RadarData* get() {
  int a = active;
  return a >= 0 ? &slots[a] : nullptr;
}

bool usableAt(double lat, double lon) {
  const RadarData* d = get();
  if (!d || !d->valid || isnan(lat) || isnan(lon)) return false;
  double dy = (lat - d->lat) * 111.0, dx = (lon - d->lon) * 111.0 * cos(lat * DEG_TO_RAD);
  return dx * dx + dy * dy < 25.0 * 25.0;  // เมืองอยู่ห่างจากกลางแผนที่ไม่เกิน 25 กม.
}

}  // namespace Radar
