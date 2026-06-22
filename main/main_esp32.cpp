#include "rForth.h"

#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>
#include <esp_littlefs.h>
#include <nvs_flash.h>

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

extern "C" void app_main()
{
  init();
  forth_init();
  mem_stat();

  char tib[256];
  size_t pos = 0;

  auto rsp_to_con = [](int, const char* rst) { printf("%s", rst); };

  while (true) {

    int c = getchar();

    if (c < 0) {
      vTaskDelay(1);
      continue;
    }

    if (c == '\r') continue;

    if (c == '\n') {
      tib[pos] = 0;
      if (pos) {
        printf("%s ", tib);
        forth_vm(tib, rsp_to_con);
      }
      pos = 0;
      vTaskDelay(1);
      continue;
    }

    if (pos < sizeof(tib) - 1) tib[pos++] = (char)c;
  }
}
