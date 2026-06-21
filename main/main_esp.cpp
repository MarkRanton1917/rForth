#include "mcu.h"

#include <Arduino.h>

extern "C" void app_main()
{
  Serial.begin(115200);
  vTaskDelay(100);

  forth_init();
  dict_init();
  mem_stat();

  String tib;
  tib.reserve(256);

  while (1) {
    auto rsp_to_con = [](int len, const char* rst) { Serial.print(rst); };

    if (Serial.available()) {
      tib = Serial.readStringUntil('\n');
      tib.trim();
      Serial.print(tib + " ");
      forth_vm(tib.c_str(), rsp_to_con);
    }
    vTaskDelay(1);
  }
}
