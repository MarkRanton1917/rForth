#include "rForth.h"

#include <iostream>
#include <string>

int main()
{
  forth_init();
  mem_stat();

  std::string tib;
  while (true) {
    std::cout << "> ";
    std::getline(std::cin, tib);
    if (tib.empty()) continue;

    auto rsp_to_con = [](int len, const char* rst) {
      std::cout.write(rst, len);
      std::cout.flush();
    };

    forth_vm(tib.c_str(), rsp_to_con);
  }
  return 0;
}
