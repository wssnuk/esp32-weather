#include "web.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "app.h"
#include "config.h"
#include "web_pages.h"

namespace {
WebServer server(80);
DNSServer dns;
bool portalMode = false;

String htmlEscape(const String& s) {
  String o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

void sendMessage(const String& title, const String& msg, const String& backUrl = "/") {
  String html = "<!doctype html><html lang='th'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<link rel='stylesheet' href='/style.css'></head><body><main><div class='card'><h1>" +
                htmlEscape(title) + "</h1><p>" + htmlEscape(msg) + "</p>";
  if (backUrl.length()) html += "<a href='" + backUrl + "'>กลับหน้าหลัก</a>";
  html += "</div></main></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleStyle() { server.send_P(200, "text/css", PAGE_STYLE); }

// ---------- โหมดตั้งค่า ----------
void handlePortal() {
  String page = FPSTR(PAGE_PORTAL);
  page.replace("%CITY%", htmlEscape(settings.city));
  server.send(200, "text/html; charset=utf-8", page);
}

void handleScan() {
  int count = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < count && i < 20; i++) {
    if (WiFi.SSID(i).isEmpty()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleSave() {
  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid.isEmpty()) {
    sendMessage("ผิดพลาด", "กรุณาใส่ชื่อ Wi-Fi");
    return;
  }
  Store::saveWifi(ssid, server.arg("pass"));
  String city = server.arg("city");
  city.trim();
  if (city.length() && city != settings.city) {
    // ยังไม่มีอินเทอร์เน็ต จะค้นหาพิกัดหลังต่อ Wi-Fi ได้ (ดูใน setup())
    Store::saveLocation(city, NAN, NAN);
  }
  sendMessage("บันทึกแล้ว ✅",
              "บอร์ดกำลังรีสตาร์ทและเชื่อมต่อ Wi-Fi \"" + ssid +
                  "\" — ให้โทรศัพท์กลับไปใช้ Wi-Fi บ้าน แล้วเปิด http://" DEVICE_HOSTNAME ".local "
                  "(หรือดู IP ใน Serial Monitor)",
              "");
  delay(1500);
  ESP.restart();
}

// ---------- โหมดปกติ ----------
void handleDashboard() { server.send_P(200, "text/html; charset=utf-8", PAGE_DASHBOARD); }

void handleApi() {
  JsonDocument doc;
  doc["city"] = settings.city;
  doc["lat"] = settings.lat;
  doc["lon"] = settings.lon;
  doc["valid"] = weather.valid;
  doc["error"] = weather.error;
  if (weather.valid) {
    doc["time"] = weather.time;
    doc["timezone"] = weather.timezone;
    doc["temp"] = weather.temp;
    doc["feelsLike"] = weather.feelsLike;
    doc["humidity"] = weather.humidity;
    doc["precipitation"] = weather.precipitation;
    doc["weatherCode"] = weather.weatherCode;
    doc["description"] = WeatherApi::describe(weather.weatherCode);
    doc["icon"] = WeatherApi::icon(weather.weatherCode, weather.isDay);
    doc["cloudCover"] = weather.cloudCover;
    doc["pressure"] = weather.pressure;
    doc["windSpeed"] = weather.windSpeed;
    doc["windDir"] = weather.windDir;
    doc["windFrom"] = WeatherApi::windDirection(weather.windDir);
    doc["tempMax"] = weather.tempMax;
    doc["tempMin"] = weather.tempMin;
    doc["rainChance"] = weather.rainChance;
    doc["uvMax"] = weather.uvMax;
    doc["sunrise"] = weather.sunrise;
    doc["sunset"] = weather.sunset;
    doc["ageSec"] = (millis() - weather.fetchedAt) / 1000;
  }
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["ip"] = WiFi.localIP().toString();
  dev["rssi"] = WiFi.RSSI();
  dev["uptime"] = millis() / 1000;
  dev["freeHeap"] = ESP.getFreeHeap();
  dev["version"] = APP_VERSION;
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

void handleRefresh() {
  requestWeatherRefresh();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleCity() {
  String msg;
  bool ok = changeCity(server.arg("city"), msg);
  sendMessage(ok ? "เปลี่ยนเมืองแล้ว ✅" : "ไม่สำเร็จ", msg);
}

void handleWifiReset() {
  sendMessage("กำลังเข้าโหมดตั้งค่า",
              "ให้เชื่อมต่อ Wi-Fi ชื่อ \"" SETUP_AP_SSID "\" จากโทรศัพท์ แล้วตั้งค่าใหม่", "");
  delay(1000);
  resetWifiAndRestart();
}
}  // namespace

namespace Web {

void beginPortal() {
  portalMode = true;
  dns.start(53, "*", WiFi.softAPIP());  // ทุกโดเมนชี้มาที่บอร์ด -> โทรศัพท์เด้งหน้าตั้งค่าเอง
  server.on("/style.css", handleStyle);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handlePortal);  // ครอบคลุม captive-portal check ของ Android/iOS
  server.begin();
}

void beginDashboard() {
  portalMode = false;
  if (MDNS.begin(DEVICE_HOSTNAME)) MDNS.addService("http", "tcp", 80);
  server.on("/", handleDashboard);
  server.on("/style.css", handleStyle);
  server.on("/api/weather", handleApi);
  server.on("/refresh", HTTP_POST, handleRefresh);
  server.on("/city", HTTP_POST, handleCity);
  server.on("/wifi-reset", handleWifiReset);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void loop() {
  if (portalMode) dns.processNextRequest();
  server.handleClient();
}

}  // namespace Web
