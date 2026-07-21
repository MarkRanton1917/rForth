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

// Not part of the library - a plain app-defined function, registered below as
// the "greet" word to demonstrate forth_dict_add().
void greet()
{
  size_t t = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  size_t f = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  int64_t p = 1000L * f / t;
  printf("rForth on ESP32 core[%d] at %d MHz, RAM %f%% free (%d/%d KB)\n", xPortGetCoreID(),
    CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, static_cast<float>(p) * 0.1, f >> 10, t >> 10);
}

static const Code words[] = {
  CODE("greet", greet()),
};

// Host-side implementation of rForth.h's ForthFile interface, backing the
// library's file-access words (open-file, read-line, etc.). Plain stdio
// works here because init() below mounts LittleFS as a POSIX VFS at
// "/littlefs" - Forth paths must be given relative to that mount point.
class PosixForthFile : public ForthFile {
  FILE* fp;

public:
  explicit PosixForthFile(FILE* f)
    : fp(f)
  {
  }
  void close() override
  {
    fclose(fp);
  }
  long read(char* buf, long len) override
  {
    return (long)fread(buf, 1, len, fp);
  }
  long write(const char* buf, long len) override
  {
    return (long)fwrite(buf, 1, len, fp);
  }
  long read_line(char* buf, long max_len) override
  {
    if (!fgets(buf, max_len, fp)) return -1;
    long n = (long)strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
      buf[--n] = '\0';
    return n;
  }
  bool seek(long pos) override
  {
    return fseek(fp, pos, SEEK_SET) == 0;
  }
  long position() override
  {
    return ftell(fp);
  }
  long size() override
  {
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long end = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return end;
  }
};

// create=true (CREATE-FILE) truncates; create=false (OPEN-FILE) requires the
// file to already exist, so w/o|r/w both map to "r+" to avoid truncating it.
static const char* fam_mode(int fam, bool create)
{
  if (create) return fam == FAM_WO ? "w" : "w+";
  return fam == FAM_RO ? "r" : "r+";
}

// The two hooks rForth.h declares under "to implement" for file access -
// the library calls these, never touching LittleFS/stdio itself.
ForthFile* forth_file_open(const char* path, int fam, bool create)
{
  FILE* fp = fopen(path, fam_mode(fam, create));
  return fp ? new PosixForthFile(fp) : nullptr;
}

bool forth_file_delete(const char* path)
{
  return remove(path) == 0;
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
  forth_dict_add(words, sizeof(words) / sizeof(Code));
  greet();

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
