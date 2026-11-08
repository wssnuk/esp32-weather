#include "settings.h"
#include <Preferences.h>
#include "config.h"

namespace {
Preferences prefs;
const char* NS = "weather";
}

namespace Store {

void load(Settings& s) {
  prefs.begin(NS, true);
  s.wifiSsid = prefs.getString("ssid", "");
  s.wifiPass = prefs.getString("pass", "");
  s.city = prefs.getString("city", DEFAULT_CITY);
  s.lat = prefs.getDouble("lat", DEFAULT_LAT);
  s.lon = prefs.getDouble("lon", DEFAULT_LON);
  s.displayMode = prefs.getUChar("disp", DEFAULT_DISPLAY_MODE);
  s.addrTh = prefs.getString("addr", "");
  s.locManual = prefs.getBool("lman", false);
  prefs.end();
}

void saveWifi(const String& ssid, const String& pass) {
  prefs.begin(NS, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void saveLocation(const String& city, double lat, double lon) {
  prefs.begin(NS, false);
  prefs.putString("city", city);
  prefs.putDouble("lat", lat);
  prefs.putDouble("lon", lon);
  prefs.end();
}

void clearWifi() {
  prefs.begin(NS, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void saveDisplayMode(uint8_t mode) {
  prefs.begin(NS, false);
  prefs.putUChar("disp", mode);
  prefs.end();
}

void saveAddress(const String& addrTh) {
  prefs.begin(NS, false);
  prefs.putString("addr", addrTh);
  prefs.end();
}

void saveLocManual(bool manual) {
  prefs.begin(NS, false);
  prefs.putBool("lman", manual);
  prefs.end();
}

}  // namespace Store
