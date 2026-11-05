#include "weather.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {

String urlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// ดาวน์โหลด JSON ผ่าน HTTPS
bool getJson(const String& url, JsonDocument& doc, String& err) {
  WiFiClientSecure client;
  client.setInsecure();  // ข้อมูลสาธารณะ ไม่มีข้อมูลลับ จึงไม่ตรวจใบรับรองเพื่อความง่าย
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, url)) {
    err = "เชื่อมต่อเซิร์ฟเวอร์ไม่ได้";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = "HTTP " + String(code);
    http.end();
    return false;
  }
  DeserializationError je = deserializeJson(doc, http.getString());
  http.end();
  if (je) {
    err = String("JSON error: ") + je.c_str();
    return false;
  }
  return true;
}

float f(JsonVariantConst v) { return v.isNull() ? NAN : v.as<float>(); }
int n(JsonVariantConst v) { return v.isNull() ? -1 : v.as<int>(); }

String hhmm(const String& iso) {  // "2026-11-05T06:12" -> "06:12"
  int t = iso.indexOf('T');
  return t >= 0 ? iso.substring(t + 1) : iso;
}

}  // namespace

namespace WeatherApi {

bool fetch(double lat, double lon, Weather& w) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) +
               "&longitude=" + String(lon, 4) +
               "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
               "precipitation,weather_code,cloud_cover,pressure_msl,wind_speed_10m,"
               "wind_direction_10m"
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
               "uv_index_max,sunrise,sunset"
               "&hourly=precipitation_probability&forecast_hours=3"
               "&timezone=auto&forecast_days=4";
  JsonDocument doc;
  String err;
  if (!getJson(url, doc, err)) {
    w.error = err;
    return false;
  }
  JsonObjectConst c = doc["current"];
  JsonObjectConst d = doc["daily"];
  if (c.isNull()) {
    w.error = doc["reason"] | "ข้อมูลไม่ถูกต้อง";
    return false;
  }
  w.time = c["time"] | "";
  w.timezone = doc["timezone"] | "";
  w.utcOffset = doc["utc_offset_seconds"] | 0L;
  w.temp = f(c["temperature_2m"]);
  w.feelsLike = f(c["apparent_temperature"]);
  w.humidity = n(c["relative_humidity_2m"]);
  w.precipitation = f(c["precipitation"]);
  w.weatherCode = n(c["weather_code"]);
  w.cloudCover = n(c["cloud_cover"]);
  w.pressure = f(c["pressure_msl"]);
  w.windSpeed = f(c["wind_speed_10m"]);
  w.windDir = n(c["wind_direction_10m"]);
  w.isDay = (c["is_day"] | 1) == 1;
  w.tempMax = f(d["temperature_2m_max"][0]);
  w.tempMin = f(d["temperature_2m_min"][0]);
  w.rainChance = n(d["precipitation_probability_max"][0]);
  w.uvMax = f(d["uv_index_max"][0]);
  w.sunrise = hhmm(d["sunrise"][0] | "");
  w.sunset = hhmm(d["sunset"][0] | "");
  for (int i = 0; i < 3; i++) {
    DayForecast& day = w.days[i];
    day.date = String((const char*)(d["time"][i + 1] | ""));
    day.code = n(d["weather_code"][i + 1]);
    day.tmax = f(d["temperature_2m_max"][i + 1]);
    day.tmin = f(d["temperature_2m_min"][i + 1]);
    day.rainChance = n(d["precipitation_probability_max"][i + 1]);
  }
  w.rainNext2h = -1;
  JsonArrayConst hp = doc["hourly"]["precipitation_probability"];
  for (size_t i = 0; i < hp.size() && i < 3; i++)
    if (!hp[i].isNull()) w.rainNext2h = max(w.rainNext2h, hp[i].as<int>());
  w.fetchedAt = millis();
  w.error = "";
  w.valid = true;
  return true;
}

bool fetchAir(double lat, double lon, AirQuality& a) {
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(lat, 4) +
               "&longitude=" + String(lon, 4) + "&current=pm2_5,pm10,us_aqi&timezone=auto";
  JsonDocument doc;
  String err;
  if (!getJson(url, doc, err)) {
    a.error = err;
    return false;
  }
  JsonObjectConst c = doc["current"];
  if (c.isNull()) {
    a.error = doc["reason"] | "ข้อมูลไม่ถูกต้อง";
    return false;
  }
  a.pm25 = f(c["pm2_5"]);
  a.pm10 = f(c["pm10"]);
  a.usAqi = n(c["us_aqi"]);
  a.error = "";
  a.valid = !isnan(a.pm25);
  return a.valid;
}

int pm25Level(float v) {
  if (isnan(v)) return -1;
  if (v <= 15) return 0;
  if (v <= 25) return 1;
  if (v <= 37.5) return 2;
  if (v <= 75) return 3;
  return 4;
}

const char* pm25LevelName(int level) {
  static const char* names[] = {"ดีมาก", "ดี", "ปานกลาง", "เริ่มมีผลกระทบต่อสุขภาพ", "มีผลกระทบต่อสุขภาพ"};
  return (level >= 0 && level <= 4) ? names[level] : "-";
}

int weekday(const String& date) {
  if (date.length() < 10) return -1;
  int y = date.substring(0, 4).toInt(), m = date.substring(5, 7).toInt(), d = date.substring(8, 10).toInt();
  if (m < 1 || m > 12) return -1;
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool geocode(const String& city, double& lat, double& lon, String& displayName) {
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
               urlEncode(city);
  JsonDocument doc;
  String err;
  if (!getJson(url, doc, err)) return false;
  JsonObjectConst r = doc["results"][0];
  if (r.isNull()) return false;
  lat = r["latitude"];
  lon = r["longitude"];
  displayName = String((const char*)(r["name"] | city.c_str()));
  const char* country = r["country"] | "";
  if (strlen(country)) displayName += String(", ") + country;
  return true;
}

const char* describe(int code) {
  switch (code) {
    case 0: return "ท้องฟ้าแจ่มใส";
    case 1: return "มีเมฆเล็กน้อย";
    case 2: return "มีเมฆบางส่วน";
    case 3: return "เมฆมาก";
    case 45: case 48: return "มีหมอก";
    case 51: case 53: case 55: return "ฝนปรอย";
    case 56: case 57: return "ฝนปรอยเยือกแข็ง";
    case 61: return "ฝนตกเล็กน้อย";
    case 63: return "ฝนตกปานกลาง";
    case 65: return "ฝนตกหนัก";
    case 66: case 67: return "ฝนเยือกแข็ง";
    case 71: case 73: case 75: case 77: return "หิมะตก";
    case 80: return "ฝนตกเป็นช่วง ๆ";
    case 81: return "ฝนตกเป็นช่วง ๆ ปานกลาง";
    case 82: return "ฝนตกหนักเป็นช่วง ๆ";
    case 85: case 86: return "หิมะตกเป็นช่วง ๆ";
    case 95: return "พายุฝนฟ้าคะนอง";
    case 96: case 99: return "พายุฝนฟ้าคะนองและลูกเห็บ";
    default: return "ไม่ทราบ";
  }
}

const char* icon(int code, bool isDay) {
  if (code == 0) return isDay ? "☀️" : "🌙";
  if (code == 1 || code == 2) return isDay ? "⛅" : "☁️";
  if (code == 3) return "☁️";
  if (code == 45 || code == 48) return "🌫️";
  if (code >= 51 && code <= 67) return "🌧️";
  if (code >= 71 && code <= 77) return "❄️";
  if (code >= 80 && code <= 82) return "🌦️";
  if (code == 85 || code == 86) return "🌨️";
  if (code >= 95) return "⛈️";
  return "❔";
}

const char* windDirection(int deg) {
  static const char* dirs[] = {"เหนือ", "ตะวันออกเฉียงเหนือ", "ตะวันออก", "ตะวันออกเฉียงใต้",
                               "ใต้", "ตะวันตกเฉียงใต้", "ตะวันตก", "ตะวันตกเฉียงเหนือ"};
  if (deg < 0) return "-";
  return dirs[((deg + 22) % 360) / 45];
}

}  // namespace WeatherApi
