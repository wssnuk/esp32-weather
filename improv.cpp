#include "improv.h"
#include <WiFi.h>
#include "app.h"
#include "config.h"

namespace {
const uint8_t HEADER[6] = {'I', 'M', 'P', 'R', 'O', 'V'};
enum : uint8_t { TYPE_STATE = 1, TYPE_ERROR = 2, TYPE_RPC = 3, TYPE_RESULT = 4 };
enum : uint8_t { STATE_READY = 2, STATE_PROVISIONING = 3, STATE_PROVISIONED = 4 };
enum : uint8_t { ERR_NONE = 0, ERR_INVALID = 1, ERR_UNKNOWN = 2, ERR_CONNECT = 3 };
enum : uint8_t { CMD_WIFI = 1, CMD_STATE = 2, CMD_INFO = 3, CMD_SCAN = 4 };

uint8_t buf[272];
size_t len = 0;

void send(uint8_t type, const uint8_t* data, uint8_t n) {
  uint8_t pkt[6 + 3 + 255 + 1];
  memcpy(pkt, HEADER, 6);
  pkt[6] = 1;  // version
  pkt[7] = type;
  pkt[8] = n;
  memcpy(pkt + 9, data, n);
  uint8_t sum = 0;
  for (int i = 0; i < 9 + n; i++) sum += pkt[i];
  pkt[9 + n] = sum;
  Serial.write(pkt, 10 + n);
  Serial.write('\n');
  Serial.flush();
}

void sendState(uint8_t s) { send(TYPE_STATE, &s, 1); }
void sendError(uint8_t e) { send(TYPE_ERROR, &e, 1); }

void sendResult(uint8_t cmd, std::initializer_list<String> strs) {
  uint8_t data[255];
  uint8_t n = 2;
  for (const String& s : strs) {
    uint8_t l = min<size_t>(s.length(), 250 - n);
    data[n++] = l;
    memcpy(data + n, s.c_str(), l);
    n += l;
  }
  data[0] = cmd;
  data[1] = n - 2;
  send(TYPE_RESULT, data, n);
}

String deviceUrl() { return "http://" + WiFi.localIP().toString() + "/"; }

void handleRpc(const uint8_t* d, uint8_t n) {
  if (n < 2) { sendError(ERR_INVALID); return; }
  uint8_t cmd = d[0];
  switch (cmd) {
    case CMD_STATE:
      if (WiFi.status() == WL_CONNECTED) {
        sendState(STATE_PROVISIONED);
        sendResult(CMD_STATE, {deviceUrl()});
      } else {
        sendState(STATE_READY);
      }
      break;
    case CMD_INFO:
      sendResult(CMD_INFO, {APP_NAME, APP_VERSION, "ESP32", DEVICE_HOSTNAME});
      break;
    case CMD_SCAN: {
      int count = WiFi.scanNetworks();
      for (int i = 0; i < count && i < 20; i++) {
        if (WiFi.SSID(i).isEmpty()) continue;
        sendResult(CMD_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)),
                              WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      }
      WiFi.scanDelete();
      sendResult(CMD_SCAN, {});
      break;
    }
    case CMD_WIFI: {
      // d[1] = ความยาวข้อมูล, ตามด้วย ssid_len, ssid, pass_len, pass
      if (n < 3) { sendError(ERR_INVALID); return; }
      uint8_t sl = d[2];
      if (3 + sl >= n) { sendError(ERR_INVALID); return; }
      String ssid((const char*)d + 3, sl);
      uint8_t pl = d[3 + sl];
      String pass((const char*)d + 4 + sl, pl);
      sendState(STATE_PROVISIONING);
      WiFi.mode(WIFI_AP_STA);
      WiFi.disconnect();
      WiFi.begin(ssid.c_str(), pass.c_str());
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) delay(100);
      if (WiFi.status() != WL_CONNECTED) {
        sendError(ERR_CONNECT);
        sendState(STATE_READY);
        return;
      }
      Store::saveWifi(ssid, pass);
      sendState(STATE_PROVISIONED);
      sendResult(CMD_WIFI, {deviceUrl()});
      delay(1500);
      ESP.restart();  // เริ่มใหม่ในโหมดปกติ
      break;
    }
    default:
      sendError(ERR_UNKNOWN);
  }
}

void flush(void (*passthrough)(char)) {
  for (size_t i = 0; i < len; i++) passthrough((char)buf[i]);
  len = 0;
}
}  // namespace

namespace Improv {

bool feed(uint8_t b, void (*passthrough)(char)) {
  if (len == 0 && b != 'I') return false;
  buf[len++] = b;
  if (len <= 6) {
    if (b != HEADER[len - 1]) {
      // ไม่ใช่ Improv: ส่งไบต์ที่เก็บไว้กลับไปให้ตัวรับคำสั่ง
      flush(passthrough);
    }
    return true;
  }
  if (len < 9) return true;
  size_t total = 9 + buf[8] + 1;
  if (len < total) return true;
  // ครบแพ็กเก็ต
  uint8_t sum = 0;
  for (size_t i = 0; i < total - 1; i++) sum += buf[i];
  bool ok = sum == buf[total - 1] && buf[6] == 1;
  uint8_t type = buf[7];
  uint8_t n = buf[8];
  uint8_t data[256];
  memcpy(data, buf + 9, n);
  len = 0;
  if (!ok) { sendError(ERR_INVALID); return true; }
  if (type == TYPE_RPC) handleRpc(data, n);
  return true;
}

}  // namespace Improv
