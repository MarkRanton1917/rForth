#include "mcu.h"

extern "C" void app_main()
{
  Serial.begin(115200);
  delay(100);

  mcu_init(); ///> initialize Forth VM
  mem_stat();

  String tib;
  tib.reserve(256); ///> reserve 256 bytes for input

  while (1) {
    auto rsp_to_con = ///> redirect Forth response to console
      [](int len, const char* rst) { Serial.print(rst); };

    if (Serial.available()) {
      tib = Serial.readStringUntil('\n');
      tib.trim();
      Serial.println(tib);

      forth_vm(tib.c_str(), rsp_to_con);
    }
    vTaskDelay(1);
  }
}