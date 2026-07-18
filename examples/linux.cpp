// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#include "rForth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/sysinfo.h>
#include <termios.h>
#include <unistd.h>
#include <csignal>

void mem_stat()
{
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    unsigned long total = info.totalram;
    unsigned long free_ram = info.freeram;
    double percent = (double)free_ram / total * 100.0;
    printf("rForth on Linux, RAM %.2f%% free (%lu/%lu KB)\n", percent, free_ram / 1024, total / 1024);
  }
  else {
    printf("rForth on Linux (memory info unavailable)\n");
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
    forth_interpret(line, dumb);
  }

  fclose(file);
  return true;
}

static struct termios orig_termios;
static bool raw_mode_active = false;

static void restore_terminal()
{
  if (raw_mode_active) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void enable_raw_terminal()
{
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return;
  atexit(restore_terminal);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  raw_mode_active = true;
}

static bool input_closed = false;

static void handle_sigint(int)
{
  forth_request_interrupt();
}

static int read_char()
{
  unsigned char c;
  ssize_t n = read(STDIN_FILENO, &c, 1);
  if (n <= 0) {
    input_closed = true;
    return INPUT_BREAK;
  }
  if (c == '\r') c = '\n';

  if (!forth_waiting_input()) {
    if (c == '\n')
      std::cout << "\r\n";
    else if (c == '\b' || c == 127)
      std::cout << "\b \b";
    else
      std::cout << (char)c;
    std::cout.flush();
  }
  return c;
}

int main()
{
  forth_init();
  mem_stat();
  enable_raw_terminal();
  signal(SIGINT, handle_sigint);

  auto rsp_to_con = [](int len, const char* rst) {
    std::cout.write(rst, len);
    std::cout.flush();
  };

  std::cout << "> ";
  std::cout.flush();
  while (!input_closed) {
    int r = forth_vm(read_char, rsp_to_con);
    if (input_closed) break;
    if (r == 0) {
      std::cout << "> ";
      std::cout.flush();
    }
  }
  return 0;
}
