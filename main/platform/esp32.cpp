#include "rForth.h"

#include <freertos/FreeRTOS.h>
#include <esp_heap_caps.h>
#include <stdio.h>

void mem_stat()
{
  const char* version = GIT_VERSION;
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  printf("rForth [%s] on Core[%d] at %d MHz, RAM %f%% free (%d/%d KB)\n", version, xPortGetCoreID(),
    CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, static_cast<float>(p) * 0.1, f >> 10, t >> 10);
}

bool forth_include(const char* fname)
{
  auto dumb = [](int, const char*) {};

  FILE* file = fopen(fname, "r");
  if (!file) return false;

  char cmd[256];
  while (fgets(cmd, sizeof(cmd), file)) {
    char* p = cmd;
    while (*p) {
      if (*p == '\r' || *p == '\n') {
        *p = '\0';
        break;
      }
      ++p;
    }
    forth_vm(cmd, dumb);
  }
  fclose(file);
  return true;
}

