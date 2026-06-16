#include "mcu.h"

#include <Arduino.h>
#include <LittleFS.h>

FV<Code*> ops = {};

void mem_stat()
{
  const char* version = GIT_VERSION;
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  Serial.printf("eForth [%s] on Core[%d] at %ld MHz, RAM %f%% free (%d/%d KB)\n", version, xPortGetCoreID(),
    getCpuFrequencyMhz(), static_cast<float>(p) * 0.1, f >> 10, t >> 10);
}

bool forth_include(const char* fname)
{
  auto dumb = [](int, const char*) {};

  if (!LittleFS.begin()) {
    return false;
  }

  File file = LittleFS.open(fname, "r");
  if (!file) {
    LittleFS.end();
    return false;
  }
  while (file.available()) {
    char cmd[256], *p = cmd, c;
    while ((c = file.read()) != '\n' && p - cmd < 255) {
      *p++ = c;
    }
    *p = '\0';
    forth_vm(cmd, dumb);
  }
  file.close();
  LittleFS.end();
  return true;
}
