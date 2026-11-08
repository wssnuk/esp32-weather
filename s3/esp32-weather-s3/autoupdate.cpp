#include "autoupdate.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "net.h"
#include "config.h"
#include "display.h"

namespace {
unsigned long nextCheck = 60UL * 1000UL;  // ตรวจครั้งแรก 1 นาทีหลังเปิดเครื่อง

// เปรียบเทียบเวอร์ชันแบบ a.b.c
long verNum(const String& v) {
  int a = 0, b = 0, c = 0;
  sscanf(v.c_str(), "%d.%d.%d", &a, &b, &c);
  return a * 1000000L + b * 1000L + c;
}
}  // namespace

namespace AutoUpdate {

bool checkNow(bool verbose) {
  if (WiFi.status() != WL_CONNECTED) return false;
  Net::Guard ng;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  String url = String(AUTO_UPDATE_URL) + "?t=" + String(millis());  // กันแคช
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (verbose) Serial.printf("[Update] ตรวจเวอร์ชันไม่สำเร็จ HTTP %d\n", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getString());
  http.end();
  if (e) return false;
  String latest = doc["version"] | "";
  String bin = doc["bin"] | "";
  if (verNum(latest) <= verNum(APP_VERSION)) {
    if (verbose) Serial.printf("[Update] เป็นเวอร์ชันล่าสุดแล้ว (%s)\n", APP_VERSION);
    return false;
  }
  Serial.printf("[Update] พบเวอร์ชันใหม่ %s (ตอนนี้ %s) กำลังติดตั้ง...\n", latest.c_str(), APP_VERSION);
  Display::showUpdatingFirmware(latest);
  Net::Guard ng2;
  WiFiClientSecure dl;
  dl.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(dl, bin);
  if (r == HTTP_UPDATE_FAILED) {
    Serial.printf("[Update] ล้มเหลว: %s\n", httpUpdate.getLastErrorString().c_str());
    Display::refresh();
  }
  return r == HTTP_UPDATE_OK;
}

void loop() {
  if ((long)(millis() - nextCheck) < 0) return;
  nextCheck = millis() + AUTO_UPDATE_EVERY_MS;
  checkNow(false);
}

}  // namespace AutoUpdate
