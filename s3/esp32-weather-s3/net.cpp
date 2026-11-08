#include "net.h"
#include <esp_attr.h>

namespace {
SemaphoreHandle_t mtx = nullptr;
}

RTC_NOINIT_ATTR volatile uint8_t g_stage;
RTC_NOINIT_ATTR volatile uint8_t g_radarStage;

const char* stageName(uint8_t s) {
  static const char* n[] = {"-", "boot", "loop", "weather", "air", "market", "geo", "update", "render",
                            "radar-net", "radar-png", "radar-mix", "radar-idle", "sound"};
  return s < sizeof(n) / sizeof(n[0]) ? n[s] : "?";
}

namespace Net {
void lock() {
  if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();
  xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
}
void unlock() {
  if (mtx) xSemaphoreGiveRecursive(mtx);
}
}  // namespace Net
