#include "geo.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"

namespace {

const char* UA = "esp32-weather-s3/" APP_VERSION " (+https://github.com/wssnuk/esp32-weather)";

int request(const String& url, const String* body, String& out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent(UA);
  if (!http.begin(client, url)) return -1;
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(*body);
  } else {
    code = http.GET();
  }
  if (code > 0) out = http.getString();
  http.end();
  return code;
}

bool startsWith(const String& s, const char* p) { return s.startsWith(p); }

// จัดชื่อเขตการปกครองไทยตามคำนำหน้า
void classify(const String& v, Geo::Address& a) {
  if (v.isEmpty()) return;
  if (a.province.isEmpty() && (startsWith(v, "จังหวัด") || v == "กรุงเทพมหานคร")) a.province = v;
  else if (a.district.isEmpty() && (startsWith(v, "อำเภอ") || startsWith(v, "เขต") || startsWith(v, "กิ่งอำเภอ"))) a.district = v;
  else if (a.sub.isEmpty() && (startsWith(v, "ตำบล") || startsWith(v, "แขวง"))) a.sub = v;
}

}  // namespace

namespace Geo {

bool locate(Fix& f, String& err) {
  // สแกน Wi-Fi รอบตัว (ใช้แค่ MAC + ความแรงสัญญาณ ไม่ส่งชื่อ Wi-Fi)
  int n = WiFi.scanNetworks(false, true);
  JsonDocument req;
  req["considerIp"] = true;
  JsonArray aps = req["wifiAccessPoints"].to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.endsWith("_nomap")) continue;  // เจ้าของ Wi-Fi ขอไม่ให้ใช้ทำแผนที่
    JsonObject o = aps.add<JsonObject>();
    o["macAddress"] = WiFi.BSSIDstr(i);
    o["signalStrength"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  String body, out;
  serializeJson(req, body);
  int code = request("https://api.beacondb.net/v1/geolocate", &body, out);
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, out) && !doc["location"]["lat"].isNull()) {
      f.lat = doc["location"]["lat"];
      f.lon = doc["location"]["lng"];
      f.accuracyM = doc["accuracy"] | 0.0f;
      f.fromWifi = doc["fallback"].isNull();
      Serial.printf("[Geo] BeaconDB: %.5f, %.5f ±%.0f ม. (%s, Wi-Fi รอบตัว %d จุด)\n", f.lat, f.lon, f.accuracyM,
                    f.fromWifi ? "จาก Wi-Fi" : "จาก IP", (int)aps.size());
      return true;
    }
  }
  // สำรอง: ตำแหน่งจาก IP
  code = request("https://ipwho.is/?fields=success,latitude,longitude,city", nullptr, out);
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, out) && (doc["success"] | false)) {
      f.lat = doc["latitude"];
      f.lon = doc["longitude"];
      f.accuracyM = 20000;
      f.fromWifi = false;
      Serial.printf("[Geo] IP: %.4f, %.4f (%s)\n", f.lat, f.lon, (const char*)(doc["city"] | ""));
      return true;
    }
  }
  err = "หาตำแหน่งไม่ได้ (HTTP " + String(code) + ")";
  return false;
}

bool reverse(double lat, double lon, Address& a, String& err) {
  a = Address();
  String q = "lat=" + String(lat, 5) + "&lon=" + String(lon, 5);
  String out;
  // 1) OpenStreetMap ภาษาไทย (มีตำบลครบในต่างจังหวัด)
  if (request("https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=16&addressdetails=1&accept-language=th&" + q,
              nullptr, out) == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, out)) {
      JsonObjectConst ad = doc["address"];
      for (JsonPairConst kv : ad) classify(String((const char*)(kv.value() | "")), a);
      if (a.province.isEmpty()) a.province = String((const char*)(ad["province"] | ad["state"] | ""));
    }
  }
  // 2) เติมส่วนที่ขาดจาก BigDataCloud
  if (a.sub.isEmpty() || a.district.isEmpty() || a.province.isEmpty()) {
    if (request("https://api.bigdatacloud.net/data/reverse-geocode-client?localityLanguage=th&latitude=" + String(lat, 5) +
                    "&longitude=" + String(lon, 5), nullptr, out) == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, out))
        for (JsonVariantConst v : doc["localityInfo"]["administrative"].as<JsonArrayConst>())
          classify(String((const char*)(v["name"] | "")), a);
    }
  }
  // ชื่อภาษาอังกฤษ (สำหรับเว็บและ Serial)
  delay(1100);  // นโยบาย Nominatim: ไม่เกิน 1 ครั้ง/วินาที
  if (request("https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=14&addressdetails=1&accept-language=en&" + q,
              nullptr, out) == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, out)) {
      JsonObjectConst ad = doc["address"];
      const char* keys[] = {"city_district", "suburb", "town", "county", "city", "state"};
      for (const char* k : keys)
        if (!ad[k].isNull()) { a.latinName = (const char*)ad[k]; break; }
      a.latinName.replace(" Subdistrict", "");
      a.latinName.replace(" District", "");
      a.latinName.replace("Khet ", "");
      a.latinName.replace("Tambon ", "");
      a.latinName.replace("Amphoe ", "");
    }
  }
  if (a.province.isEmpty() && a.district.isEmpty()) {
    err = "ไม่พบชื่อพื้นที่";
    return false;
  }
  Serial.println("[Geo] ที่อยู่: " + shortThai(a) + (a.latinName.length() ? " (" + a.latinName + ")" : ""));
  return true;
}

String shortThai(const Address& a) {
  auto sh = [](String s) {
    s.replace("กรุงเทพมหานคร", "กรุงเทพฯ");
    if (s.startsWith("จังหวัด")) s = "จ." + s.substring(strlen("จังหวัด"));
    if (s.startsWith("อำเภอ")) s = "อ." + s.substring(strlen("อำเภอ"));
    if (s.startsWith("ตำบล")) s = "ต." + s.substring(strlen("ตำบล"));
    return s;
  };
  String r;
  for (const String* p : {&a.sub, &a.district, &a.province})
    if (p->length()) { if (r.length()) r += " "; r += sh(*p); }
  return r;
}

}  // namespace Geo
