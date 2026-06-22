#include "rForth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/sysinfo.h>

const Code platform_rom[] = {};

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
