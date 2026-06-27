#include "rForth.h"
#include "version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/sysinfo.h>

void mem_stat()
{
  const char* version = GIT_VERSION;
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    unsigned long total = info.totalram;
    unsigned long free_ram = info.freeram;
    double percent = (double)free_ram / total * 100.0;
    printf("rForth [%s] on Linux, RAM %.2f%% free (%lu/%lu KB)\n", version, percent, free_ram / 1024, total / 1024);
  }
  else {
    printf("rForth [%s] on Linux (memory info unavailable)\n", version);
  }
}

bool forth_include(const char* fname)
{
  auto dumb = [](int, const char*) {};

  FILE* file = fopen(fname, "r");
  if (!file) {
    return false;
  }

  char line[256];
  while (fgets(line, sizeof(line), file)) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
    forth_vm(line, dumb);
  }

  fclose(file);
  return true;
}

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
