#include "market.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "net.h"

namespace {
float f(JsonVariantConst v) { return v.isNull() ? NAN : v.as<float>(); }
}

namespace MarketApi {

bool fetch(Market& m) {
  Net::Guard ng;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, MARKET_DATA_URL)) {
    m.error = "connect failed";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    m.error = "HTTP " + String(code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError je = deserializeJson(doc, http.getString());
  http.end();
  if (je) {
    m.error = String("JSON: ") + je.c_str();
    return false;
  }
  m.ts = doc["ts"] | 0;

  JsonObjectConst g = doc["gold"];
  m.goldValid = !g.isNull() && !g["barSell"].isNull();
  if (m.goldValid) {
    m.barBuy = f(g["barBuy"]);
    m.barSell = f(g["barSell"]);
    m.ornBuy = f(g["ornBuy"]);
    m.ornSell = f(g["ornSell"]);
    m.goldChange = f(g["change"]);
    m.goldTime = String((const char*)(g["time"] | ""));
    m.goldEstimate = String((const char*)(g["source"] | "")) == "estimate";
  }

  JsonObjectConst s = doc["set"];
  m.setValid = !s.isNull() && !s["price"].isNull();
  if (m.setValid) {
    m.setPrice = f(s["price"]);
    m.setPrev = f(s["prev"]);
    m.setChange = f(s["change"]);
    m.setPct = f(s["pct"]);
    m.setState = String((const char*)(s["state"] | "closed"));
    m.sparkCount = 0;
    for (JsonVariantConst v : s["spark"].as<JsonArrayConst>()) {
      if (m.sparkCount >= 60) break;
      if (!v.isNull()) m.spark[m.sparkCount++] = v.as<float>();
    }
  }
  JsonObjectConst s50 = doc["set50"];
  m.set50Price = f(s50["price"]);
  m.set50Change = f(s50["change"]);
  m.set50Pct = f(s50["pct"]);
  m.xau = f(doc["xau"]["price"]);
  m.xauPct = f(doc["xau"]["pct"]);
  m.usdthb = f(doc["usdthb"]["price"]);
  m.usdthbChange = f(doc["usdthb"]["change"]);
  m.error = "";
  m.valid = m.goldValid || m.setValid;
  return m.valid;
}

}  // namespace MarketApi
