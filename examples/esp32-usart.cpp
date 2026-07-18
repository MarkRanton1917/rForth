// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#include "rForth.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>
#include <esp_littlefs.h>
#include <nvs_flash.h>

static void init();

void mem_stat()
{
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  printf("rForth on ESP32 core[%d] at %d MHz, RAM %f%% free (%d/%d KB)\n", xPortGetCoreID(),
    CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, static_cast<float>(p) * 0.1, f >> 10, t >> 10);
}

bool forth_include(const char* fname)
{
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
    forth_interpret(cmd, NULL);
  }
  fclose(file);
  return true;
}

static int read_char()
{
  int c;
  while ((c = getchar()) < 0)
    vTaskDelay(1);

  if (c == '\r') c = '\n';

  if (!forth_waiting_input()) {
    if (c == '\n')
      printf("\n");
    else if (c == '\b' || c == 127)
      printf("\b \b");
    else
      putchar(c);
  }
  return c;
}

extern "C" void app_main()
{
  init();
  forth_init();
  mem_stat();

  auto rsp_to_con = [](int, const char* rst) { printf("%s", rst); };

  printf("> ");
  while (true) {
    int r = forth_vm(read_char, rsp_to_con);
    if (r == 0) printf("> ");
  }
}

static void init()
{
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = true,
    .dont_mount = false,
  };

  ret = esp_vfs_littlefs_register(&conf);
  ESP_ERROR_CHECK(ret);

  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
}
