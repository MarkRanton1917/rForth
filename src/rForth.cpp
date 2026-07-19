// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#include "rForth.h"

#include <sstream>
#include <cstring>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include <limits>

#define BASE (*(DU*)(&(heap[0])))

#if ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
#define MILLIS() (esp_timer_get_time() / 1000)
#define SYS_TASK_CREATE(entry, name, stack, param, priority, handle) \
        (xTaskCreate(entry, name, stack, param, priority, (TaskHandle_t*)handle) == pdPASS)
#define SYS_TASK_DELETE(handle)    vTaskDelete((TaskHandle_t)handle)
#define SYS_TASK_SUSPEND(handle)   vTaskSuspend((TaskHandle_t)handle)
#define SYS_TASK_RESUME(handle)    vTaskResume((TaskHandle_t)handle)
#define SYS_TASK_YIELD()           taskYIELD()
#define SYS_SLEEP_MS(ms)           vTaskDelay(pdMS_TO_TICKS(ms))
#define SYS_MUTEX_CREATE()         xSemaphoreCreateRecursiveMutex()
#define SYS_MUTEX_LOCK(m)          xSemaphoreTakeRecursive(m, portMAX_DELAY)
#define SYS_MUTEX_UNLOCK(m)        xSemaphoreGiveRecursive(m)
#define SYS_MUTEX_TYPE             SemaphoreHandle_t
#define SYS_SUSPEND_ALL_TASKS()    vTaskSuspendAll()
#define SYS_RESUME_ALL_TASKS()     xTaskResumeAll()
#define THREAD_LOCAL __thread
#elif LINUX_PLATFORM
#include <thread>
#include <mutex>
#include <chrono>
#define MILLIS() (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())
#define SYS_TASK_CREATE(entry, name, stack, param, priority, handle) \
        ([&]() { \
          auto th = new std::thread(entry, param); \
          *(handle) = (void*)th; \
          return true; \
        }())
#define SYS_TASK_DELETE(handle)    do { \
          std::thread* th = (std::thread*)handle; \
          if (th && th->joinable()) { th->detach(); delete th; } \
          handle = nullptr; \
        } while(0)
#define SYS_TASK_SUSPEND(handle)   ((void)0)
#define SYS_TASK_RESUME(handle)    ((void)0)
#define SYS_TASK_YIELD()           std::this_thread::yield()
#define SYS_SLEEP_MS(ms)           std::this_thread::sleep_for(std::chrono::milliseconds(ms))
#define SYS_MUTEX_CREATE()         new std::recursive_mutex()
#define SYS_MUTEX_LOCK(m)          ((std::recursive_mutex*)m)->lock()
#define SYS_MUTEX_UNLOCK(m)        ((std::recursive_mutex*)m)->unlock()
#define SYS_MUTEX_TYPE             void*
#define SYS_SUSPEND_ALL_TASKS()    ((void)0)
#define SYS_RESUME_ALL_TASKS()     ((void)0)
#define THREAD_LOCAL thread_local
#endif

#if USE_FLOAT
#include <cmath>
#endif

#if USE_FLOAT
static void _flit(Code* c);
#endif
static void _comment(Code* c);
static void _str(Code* c);
static void _lit(Code* c);
static void _var(Code* c);
static void _tor(Code* c);
static void _tor2(Code* c);
static void _if(Code* c);
static void _begin(Code* c);
static void _loop(Code* c);
static void _plus_loop(Code* c);
static void _abort(Code* c);
static void _does(Code* c);

static void unnest();
static std::string read_word(const char* delim = nullptr);
static void see(Code* c);
static void words();
static void load(const char* fn);
static Code* find(std::string s);
template<typename Fn>
static void forth_print(Fn fn);
static void abort_all_tasks();
static void backtrace();
static void forth_core(std::string idiom);
static void forth_task_entry(void* pvParameters);
static std::optional<DU> parse_number(const std::string& s);
static std::optional<std::pair<DU, DU>> parse_double(const std::string& s);
#if USE_FLOAT
static std::optional<DF> parse_float(const std::string& s);
#endif

static void _locals_enter(Code* c);
static void _local_fetch(Code* c);
static void _local_store(Code* c);
static size_t locals_base();
static size_t rs_marker_pos();
#if USE_FLOAT
static void _locals_enter_f(Code* c);
static void _local_fetch_f(Code* c);
static void _local_store_f(Code* c);
static size_t locals_base_f();
#endif

static FV<std::shared_ptr<Code>> dict;
static uint8_t heap[HEAP_SIZE];
static size_t heap_ptr = 0;
static int core_boundary = 0;
static bool compile = false;
static Code* last;
static FV<Local> current_locals;
#if USE_FLOAT
static FV<Local> current_locals_f;
#endif

static std::istringstream fin;
static void (*fout_cb)(int, const char*) = nullptr;
static int (*fin_cb)() = nullptr;
static std::string fin_buffer = "";
static std::atomic<bool> waiting_input { false };
static std::atomic<bool> interrupt_requested { false };

static THREAD_LOCAL ForthContext* current_ctx = nullptr;
static FV<ForthContext*> all_contexts;
static SYS_MUTEX_TYPE forth_mutex = nullptr;
static std::atomic<bool> abort_requested { false };
static std::string abort_message;
static ForthContext* abort_ctx = nullptr;

static inline void dict_push(std::shared_ptr<Code> c)
{
  last = c.get();
  dict.push(std::move(c));
}

static inline Code* dict_pop()
{
  dict.pop();
  return last = dict[-1].get();
}

static inline Code* bran_tgt()
{
  return dict[-2]->pf[-1].get();
}

static inline std::shared_ptr<Code> unowned_ref(const Code* c)
{
  return std::shared_ptr<Code>(const_cast<Code*>(c), [](Code*) {});
}

static inline Code* compiling_root()
{
  for (int i = (int)dict.size() - 1; i >= 0; --i)
    if (dict[i]->code_type != CodeType::TMP) return dict[i].get();
  return nullptr;
}

static inline void allot(size_t n)
{
  heap_ptr += n;
  if (heap_ptr > HEAP_SIZE) {
    throw std::runtime_error("Heap overflow");
  }
}

static inline DU2 pack_double(DU lo, DU hi)
{
  uint64_t uv = (static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32) | static_cast<uint32_t>(lo);
  return static_cast<DU2>(uv);
}

static inline std::pair<DU, DU> unpack_double(DU2 v)
{
  uint64_t uv = static_cast<uint64_t>(v);
  DU lo = static_cast<DU>(uv & 0xFFFFFFFFu);
  DU hi = static_cast<DU>((uv >> 32) & 0xFFFFFFFFu);
  return std::make_pair(lo, hi);
}

static const Code rom[] = {
  CODE("bye", exit(0)),
  CODE("+",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(a + b);
    }),
  CODE("-",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(a - b);
    }),
  CODE("*",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(a * b);
    }),
  CODE("/",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(a / b);
    }),
  CODE("mod",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(MOD(a, b));
    }),
  CODE("*/",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      DU c = ss_pop();
      ss_push(a * b / c);
    }),
  CODE("/mod",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      DU m = MOD(a, b);
      ss_push(m);
      ss_push(a / b);
    }),
  CODE("*/mod",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      DU c = ss_pop();
      DU2 n = (DU2)a * b;
      DU2 m = MOD(n, c);
      ss_push((DU)m);
      ss_push((DU)(n / c));
    }),
  CODE("s>d",
    {
      DU n = ss_pop();
      auto d = unpack_double((DU2)n);
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("d>s",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      (void)hi;
      ss_push(lo);
    }),
  CODE("d+",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      auto d = unpack_double(pack_double(lo1, hi1) + pack_double(lo2, hi2));
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("d-",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      auto d = unpack_double(pack_double(lo1, hi1) - pack_double(lo2, hi2));
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("dnegate",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      auto d = unpack_double(-pack_double(lo, hi));
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("dabs",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      DU2 v = pack_double(lo, hi);
      if (v < 0) v = -v;
      auto d = unpack_double(v);
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("dmax",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      DU2 a = pack_double(lo1, hi1);
      DU2 b = pack_double(lo2, hi2);
      auto d = unpack_double((a > b) ? a : b);
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("dmin",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      DU2 a = pack_double(lo1, hi1);
      DU2 b = pack_double(lo2, hi2);
      auto d = unpack_double((a < b) ? a : b);
      ss_push(d.first);
      ss_push(d.second);
    }),
  CODE("d=",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) == pack_double(lo2, hi2)));
    }),
  CODE("d<",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) < pack_double(lo2, hi2)));
    }),
  CODE("d0=",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) == 0));
    }),
  CODE("d0<",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) < 0));
    }),
  CODE("d0>",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) > 0));
    }),
  CODE("d0<>",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) != 0));
    }),
  CODE("d0>=",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) >= 0));
    }),
  CODE("d0<=",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      ss_push(BOOL(pack_double(lo, hi) <= 0));
    }),
  CODE("d>",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) > pack_double(lo2, hi2)));
    }),
  CODE("d<>",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) != pack_double(lo2, hi2)));
    }),
  CODE("d>=",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) >= pack_double(lo2, hi2)));
    }),
  CODE("d<=",
    {
      DU hi2 = ss_pop();
      DU lo2 = ss_pop();
      DU hi1 = ss_pop();
      DU lo1 = ss_pop();
      ss_push(BOOL(pack_double(lo1, hi1) <= pack_double(lo2, hi2)));
    }),
  CODE("d.",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << pack_double(lo, hi) << " "; });
    }),
  CODE("and",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(UINT(a) & UINT(b));
    }),
  CODE("or",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(UINT(a) | UINT(b));
    }),
  CODE("xor",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(UINT(a) ^ UINT(b));
    }),
  CODE("abs",
    {
      DU a = ss_pop();
      ss_push(ABS(a));
    }),
  CODE("negate",
    {
      DU a = ss_pop();
      ss_push(-a);
    }),
  CODE("invert",
    {
      DU a = ss_pop();
      ss_push(~UINT(a));
    }),
  CODE("rshift",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(UINT(a) >> UINT(b));
    }),
  CODE("lshift",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(UINT(a) << UINT(b));
    }),
  CODE("max",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push((a > b) ? a : b);
    }),
  CODE("min",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push((a < b) ? a : b);
    }),
  CODE("2*",
    {
      DU a = ss_pop();
      ss_push(a * 2);
    }),
  CODE("2/",
    {
      DU a = ss_pop();
      ss_push(a / 2);
    }),
  CODE("1+",
    {
      DU a = ss_pop();
      ss_push(a + 1);
    }),
  CODE("1-",
    {
      DU a = ss_pop();
      ss_push(a - 1);
    }),
  CODE("0=",
    {
      DU a = ss_pop();
      ss_push(BOOL(ZEQ(a)));
    }),
  CODE("0<",
    {
      DU a = ss_pop();
      ss_push(BOOL(LT(a, DU0)));
    }),
  CODE("0>",
    {
      DU a = ss_pop();
      ss_push(BOOL(GT(a, DU0)));
    }),
  CODE("=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(EQ(a, b)));
    }),
  CODE(">",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(GT(a, b)));
    }),
  CODE("<",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(LT(a, b)));
    }),
  CODE("<>",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(!EQ(a, b)));
    }),
  CODE(">=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(!LT(a, b)));
    }),
  CODE("<=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(!GT(a, b)));
    }),
  CODE("0<>",
    {
      DU a = ss_pop();
      ss_push(BOOL(!ZEQ(a)));
    }),
  CODE("0>=",
    {
      DU a = ss_pop();
      ss_push(BOOL(!LT(a, DU0)));
    }),
  CODE("0<=",
    {
      DU a = ss_pop();
      ss_push(BOOL(!GT(a, DU0)));
    }),
  CODE("u<",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) < UINT(b)));
    }),
  CODE("u>",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) > UINT(b)));
    }),
  CODE("u<=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) <= UINT(b)));
    }),
  CODE("u>=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) >= UINT(b)));
    }),
  CODE("u=",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) == UINT(b)));
    }),
  CODE("u<>",
    {
      DU b = ss_pop();
      DU a = ss_pop();
      ss_push(BOOL(UINT(a) != UINT(b)));
    }),
  CODE("dup",
    {
      DU a = ss_pop();
      ss_push(a);
      ss_push(a);
    }),
  CODE("drop", { ss_pop(); }),
  CODE("swap",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      ss_push(a);
      ss_push(b);
    }),
  CODE("over",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      ss_push(b);
      ss_push(a);
      ss_push(b);
    }),
  CODE("rot",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      DU c = ss_pop();
      ss_push(b);
      ss_push(a);
      ss_push(c);
    }),
  CODE("pick",
    {
      DU n = ss_pop();
      if (n < 0 || n >= (DU)current_ctx->ss.size()) throw std::runtime_error("Pick out of range");
      DU v = current_ctx->ss[current_ctx->ss.size() - 1 - n];
      ss_push(v);
    }),
  CODE("nip",
    {
      DU a = ss_pop();
      ss_pop();
      ss_push(a);
    }),
  CODE("-rot",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      DU c = ss_pop();
      ss_push(a);
      ss_push(c);
      ss_push(b);
    }),
  CODE("tuck",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      ss_push(a);
      ss_push(b);
      ss_push(a);
    }),
  CODE("?dup",
    {
      DU a = ss_pop();
      if (a != DU0) {
        ss_push(a);
        ss_push(a);
      }
      else
        ss_push(a);
    }),
  CODE("2dup",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      ss_push(b);
      ss_push(a);
      ss_push(b);
      ss_push(a);
    }),
  CODE("2drop",
    {
      ss_pop();
      ss_pop();
    }),
  CODE("2swap",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      DU c = ss_pop();
      DU d = ss_pop();
      ss_push(b);
      ss_push(a);
      ss_push(d);
      ss_push(c);
    }),
  CODE("2over",
    {
      DU a = ss_pop();
      DU b = ss_pop();
      DU c = ss_pop();
      DU d = ss_pop();
      ss_push(d);
      ss_push(c);
      ss_push(b);
      ss_push(a);
      ss_push(d);
      ss_push(c);
    }),
  CODE(">r", current_ctx->rs.push(ss_pop())),
  CODE("r>", ss_push(current_ctx->rs.pop())),
  CODE("r@", ss_push(current_ctx->rs[-1])),
  CODE("base", ss_push(BASE)),
  CODE("decimal",
    {
      BASE = 10;
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE); });
    }),
  CODE("hex",
    {
      BASE = 16;
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE); });
    }),
  CODE("bl", ss_push(0x20)),
  CODE("cr", { forth_print([&](std::ostringstream& os) { os << ENDL; }); }),
  CODE(".",
    {
      DU v = ss_pop();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << v << " "; });
    }),
  CODE(".r",
    {
      DU w = ss_pop();
      DU v = ss_pop();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << std::setw(w) << v; });
    }),
  CODE("u.r",
    {
      DU w = ss_pop();
      DU v = ss_pop();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << std::setw(w) << ABS(v); });
    }),
  CODE("ascii",
    {
      std::string s = read_word();
      if (s.length() == 1)
        ss_push((DU)s[0]);
      else
        throw std::runtime_error("Invalid ASCII character");
    }),
  CODE("key",
    {
      waiting_input = true;
      int input;
      if (current_ctx->key_peek != INPUT_NONE) {
        input = current_ctx->key_peek;
        current_ctx->key_peek = INPUT_NONE;
      }
      else {
        while ((input = fin_cb()) == INPUT_NONE)
          SYS_SLEEP_MS(10);
      }
      waiting_input = false;
      if (input == INPUT_BREAK) throw std::runtime_error("User interrupt");
      ss_push((DU)input);
    }),
  CODE("key?",
    {
      if (current_ctx->key_peek == INPUT_NONE) {
        waiting_input = true;
        current_ctx->key_peek = fin_cb();
        waiting_input = false;
      }
      if (current_ctx->key_peek == INPUT_BREAK) {
        current_ctx->key_peek = INPUT_NONE;
        throw std::runtime_error("User interrupt");
      }
      ss_push(BOOL(current_ctx->key_peek != INPUT_NONE));
    }),
  CODE("accept",
    {
      DU n1 = ss_pop();
      char* addr = reinterpret_cast<char*>(ss_pop());
      int count = 0;
      for (;;) {
        waiting_input = true;
        int input;
        if (current_ctx->key_peek != INPUT_NONE) {
          input = current_ctx->key_peek;
          current_ctx->key_peek = INPUT_NONE;
        }
        else {
          while ((input = fin_cb()) == INPUT_NONE)
            SYS_SLEEP_MS(10);
        }
        waiting_input = false;
        if (input == INPUT_BREAK) throw std::runtime_error("User interrupt");
        if (input == '\n' || input == '\r') {
          forth_print([&](std::ostringstream& os) { os << ENDL; });
          break;
        }
        if (input == '\b') {
          if (count > 0) {
            count--;
            forth_print([&](std::ostringstream& os) { os << (char)'\b'; });
          }
          continue;
        }
        if (count < (int)n1) {
          addr[count++] = (char)input;
          forth_print([&](std::ostringstream& os) { os << (char)input; });
        }
      }
      ss_push(count);
    }),
  CODE("number",
    {
      DU len = ss_pop();
      const char* addr = reinterpret_cast<const char*>(ss_pop());
      std::string s(addr, (size_t)len);
      if (auto dbl = parse_double(s)) {
        ss_push(dbl->first);
        ss_push(dbl->second);
        return;
      }
#if USE_FLOAT
      if (auto fval = parse_float(s)) {
        fs_push(*fval);
        return;
      }
#endif
      auto n = parse_number(s);
      if (!n) throw std::runtime_error("Invalid number");
      ss_push(*n);
    }),
  CODE("number?",
    {
      DU len = ss_pop();
      const char* addr = reinterpret_cast<const char*>(ss_pop());
      std::string s(addr, (size_t)len);
      if (auto dbl = parse_double(s)) {
        ss_push(dbl->first);
        ss_push(dbl->second);
        ss_push(BOOL(true));
        return;
      }
#if USE_FLOAT
      if (auto fval = parse_float(s)) {
        fs_push(*fval);
        ss_push(BOOL(true));
        return;
      }
#endif
      auto n = parse_number(s);
      if (n) {
        ss_push(*n);
        ss_push(BOOL(true));
      }
      else {
        ss_push((DU)addr);
        ss_push(len);
        ss_push(BOOL(false));
      }
    }),
  CODE("emit",
    {
      char ch = (char)ss_pop();
      forth_print([&](std::ostringstream& os) { os << ch; });
    }),
  CODE("space", { forth_print([&](std::ostringstream& os) { os << ' '; }); }),
  CODE("spaces",
    {
      DU n = ss_pop();
      forth_print([&](std::ostringstream& os) { os << std::setw(n) << ""; });
    }),
  CODE("type",
    {
      DU len = ss_pop();
      const char* addr = reinterpret_cast<const char*>(ss_pop());
      forth_print([&](std::ostringstream& os) {
        for (DU i = 0; i < len; ++i)
          os << addr[i];
      });
    }),
  IMMD(".(",
    {
      std::string s = read_word(")").substr(1);
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(std::make_shared<Comment>(s, true));
        SYS_MUTEX_UNLOCK(forth_mutex);
        forth_print([&](std::ostringstream& os) { os << s; });
      }
      else {
        forth_print([&](std::ostringstream& os) { os << s; });
      }
    }),
  IMMD("(",
    {
      std::string s;
      getline(fin, s, ')');
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(std::make_shared<Comment>(s, false));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
    }),
  IMMD("\\",
    {
      std::string s;
      getline(fin, s, '\n');
    }),
  IMMD(".\"",
    {
      std::string s = read_word("\"").substr(1);
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(std::make_shared<Str>(s, (int)last->token, (int)last->pf.size(), true));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else {
        forth_print([&](std::ostringstream& os) { os << s; });
      }
    }),
  IMMD("s\"",
    {
      std::string s = read_word("\"").substr(1);
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(std::make_shared<Str>(s, (int)last->token, (int)last->pf.size(), false));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else {
        int len = s.length();
        if ((size_t)len + 1 > PAD_SIZE) throw std::runtime_error("PAD overflow");
        memcpy(current_ctx->pad, s.c_str(), len + 1);
        ss_push((DU)current_ctx->pad);
        ss_push(len);
      }
    }),
  IMMD("abort\"",
    {
      std::string s = read_word("\"").substr(1);
      if (compile) {
        auto c = std::make_shared<Code>("abort\"", "", _abort, 0);
        c->set_desc(s);
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(c);
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else {
        SYS_MUTEX_LOCK(forth_mutex);
        abort_message = s;
        abort_ctx = current_ctx;
        SYS_MUTEX_UNLOCK(forth_mutex);
        abort_requested.store(true);
        throw std::runtime_error(s);
      }
    }),
  CODE("abort",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      abort_message = "Aborted";
      abort_ctx = current_ctx;
      SYS_MUTEX_UNLOCK(forth_mutex);
      abort_requested.store(true);
      throw std::runtime_error("Aborted");
    }),
  CODE("<#", { current_ctx->pad_ptr = PAD_SIZE - 1; }),
  CODE("#",
    {
      DU n = ss_pop();
      DU base = BASE;
      DU digit = n % base;
      n /= base;
      if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
      char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
      current_ctx->pad[--current_ctx->pad_ptr] = ch;
      ss_push(n);
    }),
  CODE("#s",
    {
      DU n = ss_pop();
      DU base = BASE;
      while (n != 0) {
        DU digit = n % base;
        n /= base;
        if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
        char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
        current_ctx->pad[--current_ctx->pad_ptr] = ch;
      }
      ss_push(n);
    }),
  CODE("#>",
    {
      DU n = ss_pop();
      (void)n;
      ss_push((DU)(current_ctx->pad + current_ctx->pad_ptr));
      ss_push(PAD_SIZE - current_ctx->pad_ptr - 1);
    }),
  CODE("hold",
    {
      char ch = (char)ss_pop();
      if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
      current_ctx->pad[--current_ctx->pad_ptr] = ch;
    }),
  ICOMP("if",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<Bran>(_if));
      dict_push(std::make_shared<Tmp>());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("else",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->pf.merge(last->pf);
      b->stage = 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("then",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      int s = b->stage;
      if (s == 0) {
        b->pf.merge(last->pf);
        dict_pop();
      }
      else {
        b->p1.merge(last->pf);
        if (s == 1) dict_pop();
      }
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("begin",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<Bran>(_begin));
      dict_push(std::make_shared<Tmp>());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("while",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->pf.merge(last->pf);
      b->stage = 2;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("repeat",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->p1.merge(last->pf);
      dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("again",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->pf.merge(last->pf);
      dict_pop();
      b->stage = 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("until",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->pf.merge(last->pf);
      dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("do",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<Bran>(_tor2));
      last->append(std::make_shared<Bran>(nullptr));
      dict_push(std::make_shared<Tmp>());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("i", ss_push(current_ctx->rs[-1])),
  COMP("leave", { throw 0; }),
  ICOMP("loop",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->xt = _loop;
      b->name = "loop";
      b->pf.merge(last->pf);
      dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("+loop",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = bran_tgt();
      b->xt = _plus_loop;
      b->name = "+loop";
      b->pf.merge(last->pf);
      dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("exit", { unnest(); }),
  CODE("[", { compile = false; }),
  CODE("]", { compile = true; }),
  CODE(":",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      current_locals.clear();
#if USE_FLOAT
      current_locals_f.clear();
#endif
      dict_push(std::make_shared<Code>(read_word()));
      compile = true;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP(";",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      compile = false;
      current_locals.clear();
#if USE_FLOAT
      current_locals_f.clear();
#endif
      Code* exit_word = find("exit");
      if (exit_word) last->append(exit_word);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("{:",
    {
      std::string body = read_word(":}");
      std::istringstream iss(body);
      SYS_MUTEX_LOCK(forth_mutex);
      int slot_start = (int)current_locals.size();
      int slot = slot_start;
      int block_total = 0;
      int from_stack = 0;
      bool after_dash = false;
      std::string s;
      while (iss >> s) {
        if (s == "--") {
          after_dash = true;
          continue;
        }
        current_locals.push_back({ s, slot });
        slot++;
        block_total++;
        if (!after_dash) from_stack++;
      }
      auto c = std::make_shared<Code>(_locals_enter);
      c->set_desc(body);
      c->q.push((DU)block_total);
      c->q.push((DU)from_stack);
      c->q.push((DU)slot_start);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("->",
    {
      std::string s = read_word();
      SYS_MUTEX_LOCK(forth_mutex);
      int slot = -1;
      for (auto& d : current_locals)
        if (d.name == s) {
          slot = d.slot;
          break;
        }
      if (slot < 0) {
        SYS_MUTEX_UNLOCK(forth_mutex);
        throw std::runtime_error("Unknown local '" + s + "'");
      }
      auto c = std::make_shared<Code>(_local_store);
      c->set_desc(s);
      c->q.push((DU)slot);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("constant",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      dict_push(std::make_shared<Code>(read_word()));
      DU v = ss_pop();
      last->append(std::make_shared<Lit>(v));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("variable",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      dict_push(std::make_shared<Code>(read_word()));
      DU val = 0;
      last->append(std::make_shared<Var>(alloc_heap((const uint8_t*)&val, sizeof(DU))));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("immediate",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->immd = 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("execute",
    {
      Code* w = reinterpret_cast<Code*>(ss_pop());
      w->exec();
    }),
  CODE("create",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      dict_push(std::make_shared<Code>(read_word()));
      last->append(std::make_shared<Var>((DU)&heap[heap_ptr]));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("does>",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<Bran>(_does));
      last->pf[-1]->token = last->token;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("@",
    {
      DU addr = ss_pop();
      ss_push(*(DU*)addr);
    }),
  CODE("!",
    {
      DU addr = ss_pop();
      DU val = ss_pop();
      *(DU*)addr = val;
    }),
  CODE("c@",
    {
      DU addr = ss_pop();
      ss_push(*(char*)addr);
    }),
  CODE("c!",
    {
      DU addr = ss_pop();
      DU val = ss_pop();
      *(char*)addr = (char)(val & 0xFF);
    }),
  CODE("+!",
    {
      DU addr = ss_pop();
      DU val = ss_pop();
      *(DU*)addr += val;
    }),
  CODE("?",
    {
      DU addr = ss_pop();
      forth_print([&](std::ostringstream& os) { os << *(DU*)addr << " "; });
    }),
  CODE(",",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DU val = ss_pop();
      alloc_heap((const uint8_t*)&val, sizeof(DU));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("cells",
    {
      DU val = ss_pop();
      ss_push(val * sizeof(DU));
    }),
  CODE("allot",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DU n = ss_pop();
      allot(n);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("here", ss_push((DU)&heap[heap_ptr])),
  CODE("pad", ss_push((DU)current_ctx->pad)),
  CODE("'",
    {
      std::string s = read_word();
      Code* w = find(s);
      if (w)
        ss_push(reinterpret_cast<DU>(w));
      else
        throw std::runtime_error("Undefined word");
    }),
  ICOMP("[']",
    {
      std::string s = read_word();
      Code* w = find(s);
      if (!w) throw std::runtime_error("Undefined word");
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(std::make_shared<Lit>((DU)w));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else
        ss_push((DU)w);
    }),
  CODE(".s",
    {
      forth_print([&](std::ostringstream& os) {
        char buf[34];
        auto rdx = [&buf](DU v, int b) -> const char* {
          int i = 33;
          buf[i] = '\0';
          int dec = (b == 10);
          U32 n = dec ? UINT(ABS(v)) : UINT(v);
          do {
            U8 d = (U8)MOD(n, b);
            n /= b;
            buf[--i] = d > 9 ? (d - 10) + 'a' : d + '0';
          } while (n && i);
          if (dec && v < DU0) buf[--i] = '-';
          return &buf[i];
        };

        os << "<" << current_ctx->ss.size() << "> ";
        for (DU v : current_ctx->ss)
          os << rdx(v, BASE) << ' ';
      });
    }),
  CODE("words",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      words();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("see",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* w = find(read_word());
      if (w)
        see(w);
      else
        throw std::runtime_error("Undefined word");
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("depth", ss_push(current_ctx->ss.size())),
  CODE("mstat", mem_stat()),
  CODE("ms", ss_push(MILLIS())),
  CODE("rnd", ss_push(RND())),
  CODE("included",
    {
      size_t len = (size_t)(ss_pop());
      (void)len;
      const char* filename = reinterpret_cast<const char*>(static_cast<uintptr_t>(ss_pop()));
      load(filename);
    }),
  CODE("forget",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* w = find(read_word());
      if (!w) {
        SYS_MUTEX_UNLOCK(forth_mutex);
        return;
      }
      int t = std::max((int)w->token, core_boundary);
      if ((size_t)t < dict.size()) heap_ptr = dict[t]->heap_mark;
      for (int i = dict.size(); i > t; i--)
        dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("boot",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      int t = core_boundary;
      if ((size_t)t < dict.size()) heap_ptr = dict[t]->heap_mark;
      for (int i = dict.size(); i > t; i--)
        dict_pop();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("pause", { SYS_SLEEP_MS(1); }),
  CODE("delay",
    {
      DU ms = ss_pop();
      while (ms > 0) {
        DU step = ms > 10 ? 10 : ms;
        SYS_SLEEP_MS(step);
        ms -= step;
        if (interrupt_requested.exchange(false)) throw std::runtime_error("User interrupt");
      }
    }),
  CODE("stop",
    {
#if ESP_PLATFORM
      SYS_TASK_SUSPEND(NULL);
#else
      current_ctx->finished = true;
      if (current_ctx->handle != nullptr) {
          SYS_TASK_DELETE(current_ctx->handle);
      }
#endif
    }),
  CODE("task",
    {
      DU xt_addr = ss_pop();
      Code* xt = reinterpret_cast<Code*>(xt_addr);
      if (!xt) throw std::runtime_error("Invalid xt for task");
      ForthContext* new_ctx = new ForthContext();
      new_ctx->xt = xt;
      new_ctx->pf = &xt->pf;
      new_ctx->ip = 0;
      new_ctx->finished = false;
      new_ctx->handle = nullptr;
      new_ctx->active = true;
      new_ctx->pad_ptr = PAD_SIZE - 1;
      new_ctx->pad[PAD_SIZE - 1] = '\0';
      SYS_MUTEX_LOCK(forth_mutex);
      all_contexts.push_back(new_ctx);
      size_t idx = all_contexts.size() - 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
      bool ok = SYS_TASK_CREATE(forth_task_entry, "ForthTask", 4096, new_ctx, 1, &new_ctx->handle);
      if (!ok) {
        delete new_ctx;
        throw std::runtime_error("Failed to create task");
      }
      ss_push((DU)idx);
    }),
  CODE("resume",
    {
      DU id = ss_pop();
      SYS_MUTEX_LOCK(forth_mutex);
      if (id >= all_contexts.size()) {
        SYS_MUTEX_UNLOCK(forth_mutex);
        throw std::runtime_error("Invalid task id");
      }
      ForthContext* ctx = all_contexts[id];
      SYS_MUTEX_UNLOCK(forth_mutex);
      if (!ctx) throw std::runtime_error("Task context is null");
#if ESP_PLATFORM
      if (ctx->handle != nullptr) {
        SYS_TASK_RESUME(ctx->handle);
      }
      else {
        bool ok = SYS_TASK_CREATE(forth_task_entry, "ForthTask", 4096, ctx, 1, &ctx->handle);
        if (!ok) throw std::runtime_error("Failed to resume task");
      }
#else
      if (ctx->handle == nullptr || ctx->finished) {
          ctx->finished = false;
          ctx->ip = 0;
          bool ok = SYS_TASK_CREATE(forth_task_entry, "ForthTask", 4096, ctx, 1, &ctx->handle);
          if (!ok) throw std::runtime_error("Failed to resume task");
      }
#endif
    }),
  CODE("active?",
    {
      DU id = ss_pop();
      SYS_MUTEX_LOCK(forth_mutex);
      if (id >= all_contexts.size()) {
        SYS_MUTEX_UNLOCK(forth_mutex);
        throw std::runtime_error("Invalid task id");
      }
      ForthContext* ctx = all_contexts[id];
      SYS_MUTEX_UNLOCK(forth_mutex);
      bool active = false;
      if (ctx) {
#if ESP_PLATFORM
        if (ctx->handle != nullptr) {
          eTaskState state = eTaskGetState((TaskHandle_t)ctx->handle);
          active = (state != eDeleted && state != eInvalid && !ctx->finished);
        }
#else
      active = (ctx->handle != nullptr && !ctx->finished);
#endif
      }
      ss_push(BOOL(active));
    }),
#if USE_FLOAT
  CODE("f+",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(a + b);
    }),
  CODE("f-",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(a - b);
    }),
  CODE("f*",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(a * b);
    }),
  CODE("f/",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(a / b);
    }),
  CODE("f2*",
    {
      DF a = fs_pop();
      fs_push(a * 2.0);
    }),
  CODE("f2/",
    {
      DF a = fs_pop();
      fs_push(a * 0.5);
    }),
  CODE("1/f",
    {
      DF a = fs_pop();
      fs_push(1.0 / a);
    }),
  CODE("fsqrt",
    {
      DF a = fs_pop();
      fs_push(std::sqrt(a));
    }),
  CODE("f**",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(std::pow(a, b));
    }),
  CODE("fnegate",
    {
      DF a = fs_pop();
      fs_push(-a);
    }),
  CODE("fabs",
    {
      DF a = fs_pop();
      fs_push(std::fabs(a));
    }),
  CODE("fmin",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(std::fmin(a, b));
    }),
  CODE("fmax",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(std::fmax(a, b));
    }),
  CODE("fsin",
    {
      DF a = fs_pop();
      fs_push(std::sin(a));
    }),
  CODE("fcos",
    {
      DF a = fs_pop();
      fs_push(std::cos(a));
    }),
  CODE("ftan",
    {
      DF a = fs_pop();
      fs_push(std::tan(a));
    }),
  CODE("fasin",
    {
      DF a = fs_pop();
      fs_push(std::asin(a));
    }),
  CODE("facos",
    {
      DF a = fs_pop();
      fs_push(std::acos(a));
    }),
  CODE("fatan",
    {
      DF a = fs_pop();
      fs_push(std::atan(a));
    }),
  CODE("fatan2",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      fs_push(std::atan2(a, b));
    }),
  CODE("fsinh",
    {
      DF a = fs_pop();
      fs_push(std::sinh(a));
    }),
  CODE("fcosh",
    {
      DF a = fs_pop();
      fs_push(std::cosh(a));
    }),
  CODE("ftanh",
    {
      DF a = fs_pop();
      fs_push(std::tanh(a));
    }),
  CODE("fasinh",
    {
      DF a = fs_pop();
      fs_push(std::asinh(a));
    }),
  CODE("facosh",
    {
      DF a = fs_pop();
      fs_push(std::acosh(a));
    }),
  CODE("fatanh",
    {
      DF a = fs_pop();
      fs_push(std::atanh(a));
    }),
  CODE("fexp",
    {
      DF a = fs_pop();
      fs_push(std::exp(a));
    }),
  CODE("fln",
    {
      DF a = fs_pop();
      fs_push(std::log(a));
    }),
  CODE("flog",
    {
      DF a = fs_pop();
      fs_push(std::log10(a));
    }),
  CODE("falog",
    {
      DF a = fs_pop();
      fs_push(std::pow(10.0, a));
    }),
  CODE("floor",
    {
      DF a = fs_pop();
      fs_push(std::floor(a));
    }),
  CODE("fround",
    {
      DF a = fs_pop();
      fs_push(std::round(a));
    }),
  CODE("f0=",
    {
      DF a = fs_pop();
      ss_push(BOOL(a == 0.0f));
    }),
  CODE("f0<",
    {
      DF a = fs_pop();
      ss_push(BOOL(a < 0.0f));
    }),
  CODE("f<",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a < b));
    }),
  CODE("f0>",
    {
      DF a = fs_pop();
      ss_push(BOOL(a > 0.0f));
    }),
  CODE("f0<>",
    {
      DF a = fs_pop();
      ss_push(BOOL(a != 0.0f));
    }),
  CODE("f0>=",
    {
      DF a = fs_pop();
      ss_push(BOOL(a >= 0.0f));
    }),
  CODE("f0<=",
    {
      DF a = fs_pop();
      ss_push(BOOL(a <= 0.0f));
    }),
  CODE("f=",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a == b));
    }),
  CODE("f>",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a > b));
    }),
  CODE("f<>",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a != b));
    }),
  CODE("f>=",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a >= b));
    }),
  CODE("f<=",
    {
      DF b = fs_pop();
      DF a = fs_pop();
      ss_push(BOOL(a <= b));
    }),
  CODE("f~",
    {
      DF r3 = fs_pop();
      DF r2 = fs_pop();
      DF r1 = fs_pop();
      bool flag;
      if (r3 > 0.0f) {
        flag = std::fabs(r1 - r2) < r3;
      }
      else if (r3 == 0.0f) {
        flag = (r1 == r2);
      }
      else {
        flag = std::fabs(r1 - r2) < std::fabs(r3) * (std::fabs(r1) + std::fabs(r2));
      }
      ss_push(BOOL(flag));
    }),
  CODE("s>f",
    {
      DU n = ss_pop();
      fs_push((DF)n);
    }),
  CODE("d>f",
    {
      DU hi = ss_pop();
      DU lo = ss_pop();
#if ESP_PLATFORM
      uint64_t val64 = (static_cast<uint64_t>(static_cast<uint32_t>(hi)) << 32) | static_cast<uint32_t>(lo);
      fs_push(static_cast<DF>(val64));
#else
#if defined(__SIZEOF_INT128__)
    unsigned __int128 val128 = (static_cast<unsigned __int128>(static_cast<uint64_t>(hi)) << 64)
                             | static_cast<uint64_t>(lo);
    fs_push(static_cast<DF>(static_cast<long double>(val128)));
#else
    long double val = std::ldexp(static_cast<long double>(static_cast<uint64_t>(hi)), 64);
    val += static_cast<long double>(static_cast<uint64_t>(lo));
    fs_push(static_cast<DF>(val));
#endif
#endif
    }),
  CODE("f>s",
    {
      DF a = fs_pop();
      ss_push((DU)a);
    }),
  CODE("f>d",
    {
      DF a = fs_pop();
      long double x = static_cast<long double>(a);
      long double intpart;
      std::modf(x, &intpart);

      int64_t whole = static_cast<int64_t>(intpart);

      uint32_t lo = static_cast<uint32_t>(whole & 0xFFFFFFFFULL);
      uint32_t hi = static_cast<uint32_t>((whole >> 32) & 0xFFFFFFFFULL);

      ss_push(static_cast<DU>(lo));
      ss_push(static_cast<DU>(hi));
    }),
  CODE("pi", { fs_push(3.14159265358979323846); }),
  CODE("fdup",
    {
      DF a = fs_pop();
      fs_push(a);
      fs_push(a);
    }),
  CODE("fdrop", { fs_pop(); }),
  CODE("fswap",
    {
      DF a = fs_pop();
      DF b = fs_pop();
      fs_push(a);
      fs_push(b);
    }),
  CODE("fover",
    {
      DF a = fs_pop();
      DF b = fs_pop();
      fs_push(b);
      fs_push(a);
      fs_push(b);
    }),
  CODE("frot",
    {
      DF a = fs_pop();
      DF b = fs_pop();
      DF c = fs_pop();
      fs_push(b);
      fs_push(a);
      fs_push(c);
    }),
  CODE("fnip",
    {
      DF a = fs_pop();
      fs_pop();
      fs_push(a);
    }),
  CODE("ftuck",
    {
      DF a = fs_pop();
      DF b = fs_pop();
      fs_push(a);
      fs_push(b);
      fs_push(a);
    }),
  CODE("fpick",
    {
      DU n = ss_pop();
      if (n < 0 || n >= (DU)current_ctx->fs.size()) throw std::runtime_error("Fpick out of range");
      DF v = current_ctx->fs[current_ctx->fs.size() - 1 - n];
      fs_push(v);
    }),
  CODE("f@",
    {
      DU addr = ss_pop();
      DF val;
      memcpy(&val, (void*)addr, sizeof(DF));
      fs_push(val);
    }),
  CODE("f!",
    {
      DF val = fs_pop();
      DU addr = ss_pop();
      memcpy((void*)addr, &val, sizeof(DF));
    }),
  CODE("floats",
    {
      DU n = ss_pop();
      ss_push(n * sizeof(DF));
    }),
  CODE("f.",
    {
      DF v = fs_pop();
      forth_print([&](std::ostringstream& os) { os << std::setbase(10) << v << " "; });
    }),
  CODE("f.r",
    {
      DU width = ss_pop();
      DU precision = ss_pop();
      DF v = fs_pop();
      forth_print([&](std::ostringstream& os) {
        os << std::setbase(10) << std::fixed << std::setprecision((int)precision) << std::setw((int)width) << v;
      });
    }),
  CODE("f.s",
    {
      forth_print([&](std::ostringstream& os) {
        os << "<" << current_ctx->fs.size() << "> ";
        for (DF v : current_ctx->fs)
          os << v << ' ';
      });
    }),
  CODE("fvariable",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      dict_push(std::make_shared<Code>(read_word()));
      DF val = 0.0;
      DU addr = alloc_heap((const uint8_t*)&val, sizeof(DF));
      last->append(std::make_shared<Var>(addr));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("fconstant",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DF val = fs_pop();
      dict_push(std::make_shared<Code>(read_word()));
      last->append(std::make_shared<FLit>(val));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("f{:",
    {
      std::string body = read_word("f:}");
      std::istringstream iss(body);
      SYS_MUTEX_LOCK(forth_mutex);
      int slot_start = (int)current_locals_f.size();
      int slot = slot_start;
      int block_total = 0;
      int from_stack = 0;
      bool after_dash = false;
      std::string s;
      while (iss >> s) {
        if (s == "--") {
          after_dash = true;
          continue;
        }
        current_locals_f.push_back({ s, slot });
        slot++;
        block_total++;
        if (!after_dash) from_stack++;
      }
      auto c = std::make_shared<Code>(_locals_enter_f);
      c->set_desc(body);
      c->q.push((DU)block_total);
      c->q.push((DU)from_stack);
      c->q.push((DU)slot_start);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  ICOMP("f->",
    {
      std::string s = read_word();
      SYS_MUTEX_LOCK(forth_mutex);
      int slot = -1;
      for (auto& d : current_locals_f)
        if (d.name == s) {
          slot = d.slot;
          break;
        }
      if (slot < 0) {
        SYS_MUTEX_UNLOCK(forth_mutex);
        throw std::runtime_error("Unknown local '" + s + "'");
      }
      auto c = std::make_shared<Code>(_local_store_f);
      c->set_desc(s);
      c->q.push((DU)slot);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
#endif
};

void ss_push(DU n)
{
  current_ctx->ss.push(n);
}

DU ss_pop()
{
  return current_ctx->ss.pop();
}

#if USE_FLOAT
void fs_push(DF n)
{
  current_ctx->fs.push(n);
}

DF fs_pop()
{
  return current_ctx->fs.pop();
}
#endif

void dict_add(const Code* words, size_t size)
{
  dict.reserve(size * 2);
  for (int i = 0; i < size; ++i)
    dict_push(unowned_ref(&words[i]));
  core_boundary = (int)dict.size();
}

DU alloc_heap(const uint8_t* val, size_t size)
{
  SYS_MUTEX_LOCK(forth_mutex);
  DU addr = (DU)&heap[heap_ptr];
  memcpy((void*)addr, val, size);
  allot(size);
  SYS_MUTEX_UNLOCK(forth_mutex);
  return addr;
}

void forth_init()
{
  const int sz = sizeof(rom) / sizeof(Code);
  dict.reserve(sz * 2);
  for (int i = 0; i < sz; ++i)
    dict_push(unowned_ref(&rom[i]));
  core_boundary = (int)dict.size();
  heap_ptr = sizeof(DU);
  BASE = 10;
  forth_mutex = SYS_MUTEX_CREATE();
  ForthContext* ctx0 = new ForthContext();
  ctx0->pf = nullptr;
  ctx0->ip = 0;
  ctx0->finished = false;
  ctx0->handle = nullptr;
  ctx0->active = true;
  ctx0->pad_ptr = PAD_SIZE - 1;
  ctx0->pad[PAD_SIZE - 1] = '\0';
  all_contexts.push_back(ctx0);
  current_ctx = ctx0;
}

int forth_interpret(std::string input, void (*output_hook)(int, const char*))
{
  static uint err_cnt = 0;
  bool error_occured = false;

  auto next_token = [](const std::string& line, size_t start_pos) -> std::string {
    size_t i = start_pos;
    while (i < line.size() && isspace(line[i]))
      i++;
    if (i >= line.size()) return "";
    size_t j = i;
    while (j < line.size() && !isspace(line[j]))
      j++;
    return line.substr(i, j - i);
  };

  auto replace = [&](const std::string& line, const std::string& idiom,
                   const std::string& exception_what) -> std::string {
    std::string ret = line;
    if (idiom != "see" && idiom != "ascii") {
      size_t pos = line.find(idiom);
      if (pos != std::string::npos)
        ret.replace(pos, idiom.length(), ">>>" + idiom + "<<<");
      else
        ret = ">>>" + idiom + "<<<";
    }
    else {
      size_t pos = line.find(idiom);
      if (pos != std::string::npos) {
        pos += idiom.length();
        std::string nxt = next_token(line, pos);
        if (!nxt.empty()) {
          size_t nxt_pos = line.find(nxt, pos);
          if (nxt_pos != std::string::npos)
            ret.replace(nxt_pos, nxt.length(), ">>>" + nxt + "<<<");
          else
            ret = ">>>" + nxt + "<<<";
        }
        else
          ret = line + " >>><<<";
      }
      else
        ret = ">>>" + idiom + "<<<";
    }
    return ret;
  };

  std::string idiom;
  auto outer = [&](const std::string& current_line) {
    while (fin >> idiom) {
      try {
        forth_core(idiom);
      }
      catch (std::exception& e) {
        forth_print([&](std::ostringstream& os) {
          os << ENDL;
          os << ":" << err_cnt++ << ": " << e.what() << ENDL;
          std::string marked_line = replace(current_line, idiom, e.what());
          os << marked_line << ENDL;
        });
        backtrace();
        error_occured = true;
        current_ctx->ss.clear();
        current_ctx->rs.clear();
        current_ctx->ls.clear();
#if USE_FLOAT
        current_ctx->lfs.clear();
#endif
        current_ctx->call_stack.clear();
        current_ctx->pf = nullptr;
        current_ctx->ip = 0;
        current_ctx->key_peek = INPUT_NONE;
        compile = false;
        current_locals.clear();
#if USE_FLOAT
        current_locals_f.clear();
#endif
        getline(fin, idiom, '\n');
        abort_all_tasks();
        return;
      }
      catch (int) {
      }
    }
  };

  auto fout_cb_default = [](int, const char*) {};
  fout_cb = output_hook ? output_hook : fout_cb_default;

  std::istringstream istm(input);
  std::string line;

  while (getline(istm, line)) {
    fin.clear();
    fin.str(line);
    outer(line);
    if (error_occured) break;
  }
  if (!error_occured) {
    forth_print([&](std::ostringstream& os) {
      if (compile)
        os << " compiled" << ENDL;
      else
        os << " ok" << ENDL;
    });
  }
  fin_buffer = "";
  return 0;
}

int forth_vm(int (*input_hook)(), void (*output_hook)(int, const char*))
{
  auto fout_cb_default = [](int, const char*) {};
  fout_cb = output_hook ? output_hook : fout_cb_default;

  auto fin_cb_default = []() -> int { return INPUT_NONE; };
  fin_cb = input_hook ? input_hook : fin_cb_default;

  int input = fin_cb();
  switch (input) {
  case INPUT_NONE:
    return -1;
  default:
    fin_buffer += (char)input;
    break;
  }
  if (strcmp(fin_buffer.c_str() + sizeof(char) * (fin_buffer.length() - strlen(INPUT_ENDL)), INPUT_ENDL) == 0) {
    return forth_interpret(fin_buffer, output_hook);
  }
  return -1;
}

bool forth_waiting_input()
{
  return waiting_input;
}

void forth_request_interrupt()
{
  interrupt_requested.store(true);
}

static void backtrace()
{
  forth_print([&](std::ostringstream& os) {
    os << "Backtrace:" << ENDL;
    FV<Code*>* stack = nullptr;
    if (abort_ctx != nullptr) {
      stack = &abort_ctx->call_stack;
    }
    else {
      stack = &current_ctx->call_stack;
    }

    for (auto it = stack->rbegin(); it != stack->rend(); ++it) {
      const Code* c = *it;
      os << "$" << std::hex << (uintptr_t)c << " " << (c->name ? c->name : "(anon)") << std::dec << ENDL;
    }
  });
}

static void forth_task_entry(void* pvParameters)
{
  ForthContext* ctx = (ForthContext*)pvParameters;
  current_ctx = ctx;

  ctx->call_stack.clear();
  if (ctx->xt) {
    ctx->call_stack.push_back(ctx->xt);
  }

  try {
    while (true) {
      if (!ctx->pf || ctx->ip >= ctx->pf->size()) break;
      if (abort_requested.load()) break;

      Code* w = (*ctx->pf)[ctx->ip++].get();
      w->exec();
    }
  }
  catch (std::exception& e) {
    SYS_MUTEX_LOCK(forth_mutex);
    abort_message = e.what();
    abort_ctx = ctx;
    SYS_MUTEX_UNLOCK(forth_mutex);
    abort_requested.store(true);
  }
  catch (...) {
    SYS_MUTEX_LOCK(forth_mutex);
    abort_message = "unknown error";
    abort_ctx = ctx;
    SYS_MUTEX_UNLOCK(forth_mutex);
    abort_requested.store(true);
  }

  ctx->finished = true;
  ctx->pf = nullptr;
  ctx->ip = 0;

#if ESP_PLATFORM
  ctx->handle = nullptr;
  vTaskDelete(NULL);
#else
  if (ctx->handle != nullptr) {
    SYS_TASK_DELETE(ctx->handle);
  }
#endif
}

static void abort_all_tasks()
{
  abort_requested.store(true);

  const int MAX_WAIT_MS = 1000;
  int waited = 0;
  bool all_finished = false;

  while (!all_finished && waited < MAX_WAIT_MS) {
    all_finished = true;
    SYS_MUTEX_LOCK(forth_mutex);
    for (size_t i = 1; i < all_contexts.size(); ++i) {
      ForthContext* ctx = all_contexts[i];
      if (ctx && ctx->handle != nullptr && !ctx->finished) {
        all_finished = false;
        break;
      }
    }
    SYS_MUTEX_UNLOCK(forth_mutex);

    if (!all_finished) {
      SYS_SLEEP_MS(10);
      waited += 10;
    }
  }

  SYS_MUTEX_LOCK(forth_mutex);
  for (size_t i = 1; i < all_contexts.size(); ++i) {
    ForthContext* ctx = all_contexts[i];
    if (ctx) {
      if (ctx->handle != nullptr) {
        SYS_TASK_DELETE(ctx->handle);
        ctx->handle = nullptr;
      }
      delete ctx;
    }
  }
  all_contexts.resize(1);

  ForthContext* ctx0 = all_contexts[0];
  ctx0->ss.clear();
  ctx0->rs.clear();
  ctx0->ls.clear();
#if USE_FLOAT
  ctx0->lfs.clear();
#endif
  ctx0->call_stack.clear();
  ctx0->pf = nullptr;
  ctx0->ip = 0;
  ctx0->finished = false;
  ctx0->active = true;
  ctx0->key_peek = INPUT_NONE;
  current_ctx = ctx0;

  abort_message.clear();
  abort_ctx = nullptr;
  abort_requested.store(false);
  SYS_MUTEX_UNLOCK(forth_mutex);
}

static void unnest()
{
  ForthContext* ctx = current_ctx;
  bool found = false;
  size_t marker_pos = 0;
  for (int i = (int)ctx->rs.size() - 1; i >= 0; --i) {
    if (ctx->rs[i] == WORD_MARKER_FRAME) {
      marker_pos = i;
      found = true;
      break;
    }
  }
  if (!found) {
    ctx->pf = nullptr;
    ctx->ip = 0;
    if (!ctx->call_stack.empty()) ctx->call_stack.pop_back();
    return;
  }
#if USE_FLOAT
  while (ctx->rs.size() > marker_pos + 5)
    ctx->rs.pop_back();
  DU has_locals_f = ctx->rs.back();
  ctx->rs.pop_back();
#else
  while (ctx->rs.size() > marker_pos + 4)
    ctx->rs.pop_back();
#endif
  DU has_locals = ctx->rs.back();
  ctx->rs.pop_back();
  ctx->ip = (size_t)ctx->rs.back();
  ctx->rs.pop_back();
  ctx->pf = (const FV<std::shared_ptr<Code>>*)ctx->rs.back();
  ctx->rs.pop_back();
  ctx->rs.pop_back();
  if (has_locals) {
    FV<DU>& ls = ctx->ls;
    for (int i = (int)ls.size() - 1; i >= 0; --i) {
      if (ls[i] == LOCALS_MARKER_FRAME) {
        ls.resize(i);
        break;
      }
    }
  }
#if USE_FLOAT
  if (has_locals_f) {
    FV<DF>& lfs = ctx->lfs;
    for (int i = (int)lfs.size() - 1; i >= 0; --i) {
      if (lfs[i] == LOCALS_MARKER_FRAME_F) {
        lfs.resize(i);
        break;
      }
    }
  }
#endif
  if (!ctx->call_stack.empty()) ctx->call_stack.pop_back();
}

void Code::exec()
{
  struct Frame {
    const FV<std::shared_ptr<Code>>* pf;
    size_t ip;
    Code* word;
  };

  ForthContext* ctx = current_ctx;

  if (interrupt_requested.exchange(false)) {
    throw std::runtime_error("User interrupt");
  }

  if (xt != nullptr) {
    if (abort_requested.load()) {
      std::string msg;
      SYS_MUTEX_LOCK(forth_mutex);
      msg = abort_message;
      SYS_MUTEX_UNLOCK(forth_mutex);
      throw std::runtime_error(msg);
    }
    ctx->call_stack.push_back(this);
    xt(this);
    ctx->call_stack.pop_back();
    return;
  }

  FV<Frame> exec_stack;

  ctx->rs.push_back(WORD_MARKER_FRAME);
  ctx->rs.push_back((DU)ctx->pf);
  ctx->rs.push_back((DU)ctx->ip);
  ctx->rs.push_back(0);
#if USE_FLOAT
  ctx->rs.push_back(0);
#endif
  ctx->call_stack.push_back(this);

  ctx->pf = &pf;
  ctx->ip = 0;
  size_t rs_frame_start = ctx->rs.size();

  while (true) {
    if (abort_requested.load()) {
      std::string msg;
      SYS_MUTEX_LOCK(forth_mutex);
      msg = abort_message;
      SYS_MUTEX_UNLOCK(forth_mutex);
      throw std::runtime_error(msg);
    }

    while (ctx->pf == nullptr || ctx->ip >= ctx->pf->size()) {
      if (exec_stack.empty()) goto done;

      Frame& top = exec_stack.back();
      if (!ctx->call_stack.empty() && ctx->call_stack.back() == top.word) ctx->call_stack.pop_back();

      ctx->pf = top.pf;
      ctx->ip = top.ip;
      exec_stack.pop_back();
    }

    Code* w = (*ctx->pf)[ctx->ip++].get();

    if (w->xt != nullptr) {
      ctx->call_stack.push_back(w);
      w->xt(w);
      ctx->call_stack.pop_back();
      if (ctx->rs.size() < rs_frame_start) goto done;
    }
    else {
      exec_stack.push_back(Frame { ctx->pf, ctx->ip, w });
      ctx->call_stack.push_back(w);
      ctx->pf = &w->pf;
      ctx->ip = 0;
    }
  }

done:
  if (ctx->rs.size() >= rs_frame_start) unnest();
  if (!ctx->call_stack.empty() && ctx->call_stack.back() == this) ctx->call_stack.pop_back();
}

static void _does(Code* c)
{
  bool hit = false;
  SYS_MUTEX_LOCK(forth_mutex);
  for (auto& w : dict[c->token]->pf) {
    if (hit) last->append(w);
    if (STRCMP(w->name, "does>") == 0) hit = true;
  }
  SYS_MUTEX_UNLOCK(forth_mutex);
  throw 0;
}

static void _comment(Code* c)
{
}

static void _str(Code* c)
{
  if (strcmp(c->name, "s\"") == 0) {
    ss_push((DU)c->desc);
    ss_push(strlen(c->desc));
  }
  else {
    forth_print([&](std::ostringstream& os) { os << c->desc; });
  }
}

static void _lit(Code* c)
{
  ss_push(c->q[0]);
}

#if USE_FLOAT
static void _flit(Code* c)
{
  FLit* fl = static_cast<FLit*>(c);
  fs_push(fl->val);
}
#endif

static void _var(Code* c)
{
  ss_push(c->q[0]);
}

static size_t rs_marker_pos()
{
  FV<DU>& rs = current_ctx->rs;
  for (int i = (int)rs.size() - 1; i >= 0; --i) {
    if (rs[i] == WORD_MARKER_FRAME) return (size_t)i;
  }
  throw std::runtime_error("No word frame active");
}

static void _locals_enter(Code* c)
{
  int total = (int)c->q[0];
  int from_stack = (int)c->q[1];
  int slot_start = (int)c->q[2];

  std::vector<DU> tmp(from_stack);
  for (int i = from_stack - 1; i >= 0; --i)
    tmp[i] = ss_pop();

  size_t marker = rs_marker_pos();
  if (current_ctx->rs[(int)(marker + 3)] == 0) {
    current_ctx->ls.push_back(LOCALS_MARKER_FRAME);
    current_ctx->rs[(int)(marker + 3)] = 1;
  }

  size_t base = locals_base();
  size_t need = base + (size_t)slot_start + (size_t)total;
  if (current_ctx->ls.size() < need) current_ctx->ls.resize(need, DU0);

  for (int i = 0; i < total; ++i) {
    DU val = (i < from_stack) ? tmp[i] : DU0;
    current_ctx->ls[base + slot_start + i] = val;
  }
}

static void _local_fetch(Code* c)
{
  size_t base = locals_base();
  int slot = (int)c->q[0];
  ss_push(current_ctx->ls[(int)(base + (size_t)slot)]);
}

static void _local_store(Code* c)
{
  size_t base = locals_base();
  int slot = (int)c->q[0];
  DU v = ss_pop();
  current_ctx->ls[(int)(base + (size_t)slot)] = v;
}

static size_t locals_base()
{
  FV<DU>& ls = current_ctx->ls;
  for (int i = (int)ls.size() - 1; i >= 0; --i) {
    if (ls[i] == LOCALS_MARKER_FRAME) return (size_t)(i + 1);
  }
  throw std::runtime_error("No locals frame active");
}

#if USE_FLOAT
static void _locals_enter_f(Code* c)
{
  int total = (int)c->q[0];
  int from_stack = (int)c->q[1];
  int slot_start = (int)c->q[2];

  std::vector<DF> tmp(from_stack);
  for (int i = from_stack - 1; i >= 0; --i)
    tmp[i] = fs_pop();

  size_t marker = rs_marker_pos();
  if (current_ctx->rs[(int)(marker + 4)] == 0) {
    current_ctx->lfs.push_back(LOCALS_MARKER_FRAME_F);
    current_ctx->rs[(int)(marker + 4)] = 1;
  }

  size_t base = locals_base_f();
  size_t need = base + (size_t)slot_start + (size_t)total;
  if (current_ctx->lfs.size() < need) current_ctx->lfs.resize(need, (DF)0);

  for (int i = 0; i < total; ++i) {
    DF val = (i < from_stack) ? tmp[i] : (DF)0;
    current_ctx->lfs[base + slot_start + i] = val;
  }
}

static void _local_fetch_f(Code* c)
{
  size_t base = locals_base_f();
  int slot = (int)c->q[0];
  fs_push(current_ctx->lfs[(int)(base + (size_t)slot)]);
}

static void _local_store_f(Code* c)
{
  size_t base = locals_base_f();
  int slot = (int)c->q[0];
  DF v = fs_pop();
  current_ctx->lfs[(int)(base + (size_t)slot)] = v;
}

static size_t locals_base_f()
{
  FV<DF>& lfs = current_ctx->lfs;
  for (int i = (int)lfs.size() - 1; i >= 0; --i) {
    if (lfs[i] == LOCALS_MARKER_FRAME_F) return (size_t)(i + 1);
  }
  throw std::runtime_error("No locals frame active");
}
#endif

static void _tor(Code* c)
{
  current_ctx->rs.push(ss_pop());
}

static void _tor2(Code* c)
{
  DU first = ss_pop();
  DU limit = ss_pop();
  current_ctx->rs.push(limit);
  current_ctx->rs.push(first);
}

static void _if(Code* c)
{
  if (ss_pop()) {
    for (auto& w : c->pf)
      w->exec();
  }
  else {
    for (auto& w : c->p1)
      w->exec();
  }
}

static void _begin(Code* c)
{
  int b = c->stage;
  while (true) {
    for (auto& w : c->pf)
      w->exec();
    if (b == 0 && ss_pop() != 0) break;
    if (b == 1) continue;
    if (b == 2 && ss_pop() == 0) break;
    for (auto& w : c->p1)
      w->exec();
  }
}

static void _loop(Code* c)
{
  try {
    while (true) {
      for (auto& w : c->pf)
        w->exec();
      if (current_ctx->rs.size() < 2) break;
      DU index = current_ctx->rs[-1] + 1;
      DU limit = current_ctx->rs[-2];
      current_ctx->rs[-1] = index;
      if (index >= limit) {
        current_ctx->rs.pop();
        current_ctx->rs.pop();
        break;
      }
    }
  }
  catch (int) {
    if (current_ctx->rs.size() >= 2) {
      current_ctx->rs.pop();
      current_ctx->rs.pop();
    }
  }
}

static void _plus_loop(Code* c)
{
  try {
    while (true) {
      for (auto& w : c->pf)
        w->exec();
      if (current_ctx->rs.size() < 2) break;
      DU n = ss_pop();
      DU index = current_ctx->rs[-1] + n;
      DU limit = current_ctx->rs[-2];
      current_ctx->rs[-1] = index;
      bool exit_condition = (n >= 0) ? (index >= limit) : (index <= limit);
      if (exit_condition) {
        current_ctx->rs.pop();
        current_ctx->rs.pop();
        break;
      }
    }
  }
  catch (int) {
    if (current_ctx->rs.size() >= 2) {
      current_ctx->rs.pop();
      current_ctx->rs.pop();
    }
  }
}

static void _abort(Code* c)
{
  SYS_MUTEX_LOCK(forth_mutex);
  abort_message = c->desc ? c->desc : "Aborted";
  abort_ctx = current_ctx;
  SYS_MUTEX_UNLOCK(forth_mutex);
  abort_requested.store(true);
  throw std::runtime_error(abort_message);
}

static std::string read_word(const char* delim)
{
  std::string s;
  if (!delim || !*delim) {
    fin >> s;
    return s;
  }
  if (!delim[1]) {
    getline(fin, s, delim[0]);
    if (fin.eof()) throw std::runtime_error(std::string("Missing closing '") + delim + "'");
    return s;
  }
  std::string word;
  while (fin >> word) {
    if (word == delim) return s;
    if (!s.empty()) s += ' ';
    s += word;
  }
  throw std::runtime_error(std::string("Missing closing '") + delim + "'");
}

static void _see(Code* c)
{
  if (!c) return;

  if (c->xt == _comment) {
    if (strcmp(c->name, ".(") == 0) {
      forth_print([&](std::ostringstream& os) { os << ".(" << c->desc << ") "; });
    }
    else {
      forth_print([&](std::ostringstream& os) { os << "( " << c->desc << " ) "; });
    }
    return;
  }

  if (c->xt == _lit) {
    forth_print([&](std::ostringstream& os) { os << c->q[0] << " "; });
    return;
  }

  if (c->xt == _str) {
    if (strcmp(c->name, "s\"") == 0) {
      forth_print([&](std::ostringstream& os) { os << "s\" " << c->desc << "\" "; });
    }
    else {
      forth_print([&](std::ostringstream& os) { os << ".\" " << c->desc << "\" "; });
    }
    return;
  }

  if (c->xt == _abort) {
    if (strcmp(c->name, "abort\"") == 0) {
      forth_print([&](std::ostringstream& os) { os << "abort\" " << c->desc << "\" "; });
    }
    else {
      forth_print([&](std::ostringstream& os) { os << "abort "; });
    }
    return;
  }

  if (c->xt == _locals_enter) {
    forth_print([&](std::ostringstream& os) { os << "{: " << c->desc << " :} "; });
    return;
  }

  if (c->xt == _local_fetch) {
    forth_print([&](std::ostringstream& os) { os << c->desc << " "; });
    return;
  }

  if (c->xt == _local_store) {
    forth_print([&](std::ostringstream& os) { os << "-> " << c->desc << " "; });
    return;
  }

#if USE_FLOAT
  if (c->xt == _locals_enter_f) {
    forth_print([&](std::ostringstream& os) { os << "f{: " << c->desc << " f:} "; });
    return;
  }

  if (c->xt == _local_fetch_f) {
    forth_print([&](std::ostringstream& os) { os << c->desc << " "; });
    return;
  }

  if (c->xt == _local_store_f) {
    forth_print([&](std::ostringstream& os) { os << "f-> " << c->desc << " "; });
    return;
  }
#endif

  const char* nm = c->name ? c->name : "";
  if (strcmp(nm, "if") == 0) {
    forth_print([&](std::ostringstream& os) { os << "if "; });
    for (auto& w : c->pf)
      _see(w.get());
    if (c->stage == 1 && !c->p1.empty()) {
      forth_print([&](std::ostringstream& os) { os << "else "; });
      for (auto& w : c->p1)
        _see(w.get());
    }
    forth_print([&](std::ostringstream& os) { os << "then "; });
    return;
  }
  if (strcmp(nm, "begin") == 0) {
    forth_print([&](std::ostringstream& os) { os << "begin "; });
    for (auto& w : c->pf)
      _see(w.get());
    if (c->stage == 2) {
      forth_print([&](std::ostringstream& os) { os << "while "; });
      for (auto& w : c->p1)
        _see(w.get());
      forth_print([&](std::ostringstream& os) { os << "repeat "; });
    }
    else if (c->stage == 0) {
      forth_print([&](std::ostringstream& os) { os << "until "; });
    }
    else if (c->stage == 1) {
      forth_print([&](std::ostringstream& os) { os << "again "; });
    }
    return;
  }
  if (strcmp(nm, "do") == 0) {
    forth_print([&](std::ostringstream& os) { os << "do "; });
    return;
  }
  if (strcmp(nm, "loop") == 0 || strcmp(nm, "+loop") == 0) {
    for (auto& w : c->pf)
      _see(w.get());
    forth_print([&](std::ostringstream& os) { os << nm << " "; });
    return;
  }
  if (nm[0] != '\0' && nm[0] != '\t') forth_print([&](std::ostringstream& os) { os << nm << " "; });
}

static void see(Code* c)
{
  if (!c) {
    forth_print([&](std::ostringstream& os) { os << "  -> { not found }"; });
    return;
  }
  if (c->xt) {
    forth_print([&](std::ostringstream& os) { os << "  ->{ " << c->desc << "; } "; });
    return;
  }
  forth_print([&](std::ostringstream& os) { os << ": " << c->name << " "; });

  size_t n = c->pf.size();
  if (n > 0 && c->pf[n - 1] && strcmp(c->pf[n - 1]->name, "exit") == 0) {
    n--;
  }
  for (size_t i = 0; i < n; ++i) {
    _see(c->pf[i].get());
  }
  forth_print([&](std::ostringstream& os) { os << "; "; });
}

static void words()
{
  const int WIDTH = 16;
  int x = 0;
  std::string output;
  for (auto& w : dict) {
    std::string name = w->name ? w->name : "";
    output += "  " + name;
    x += (name.length() + 2);
    if (x > WIDTH) {
      forth_print([&](std::ostringstream& os) { os << output << ENDL; });
      output.clear();
      x = 0;
    }
  }
  if (!output.empty()) {
    forth_print([&](std::ostringstream& os) { os << output << ENDL; });
  }
}

static void load(const char* fn)
{
  void (*cb)(int, const char*) = fout_cb;
  std::string in;
  getline(fin, in);
  if (!forth_include(fn)) throw std::runtime_error("Can't open file");
  fout_cb = cb;
  fin.clear();
  fin.str(in);
}

static Code* find(std::string s)
{
  for (int i = dict.size() - 1; i >= 0; --i)
    if (STRCMP(s.c_str(), dict[i]->name) == 0) return dict[i].get();
  return nullptr;
}

#if USE_FLOAT
static std::optional<DF> parse_float(const std::string& s)
{
  bool has_dot = s.find('.') != std::string::npos;
  bool has_e = s.find('e') != std::string::npos || s.find('E') != std::string::npos;
  if (!has_dot && !has_e) return std::nullopt;

  char* end;
  DF v = strtod(s.c_str(), &end);

  if (*end == '\0') return v;
  if ((*end == 'e' || *end == 'E') && *(end + 1) == '\0') return v;
  return std::nullopt;
}
#endif

static std::optional<DU> parse_number(const std::string& s)
{
  const char* cs = s.c_str();
  int b = BASE;
  switch (*cs) {
  case '%':
    b = 2;
    cs++;
    break;
  case '&':
  case '#':
    b = 10;
    cs++;
    break;
  case '$':
    b = 16;
    cs++;
    break;
  }
  char* p;
  errno = 0;
  long n = strtol(cs, &p, b);
  if (errno || *p != '\0') return std::nullopt;
  if (n < (long)std::numeric_limits<DU>::min() || n > (long)std::numeric_limits<DU>::max()) return std::nullopt;
  return (DU)n;
}

static std::optional<std::pair<DU, DU>> parse_double(const std::string& s)
{
  size_t i = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
  size_t digits_start = i;
  while (i < s.size() && isdigit((unsigned char)s[i]))
    i++;
  if (i == digits_start || i != s.size() - 1 || s[i] != '.') return std::nullopt;

  std::string digits = s.substr(0, s.size() - 1);

  errno = 0;
  char* p;
  DU2 v = static_cast<DU2>(strtoll(digits.c_str(), &p, BASE));
  if (errno || *p != '\0') return std::nullopt;

  return unpack_double(v);
}

template<typename Fn>
static void forth_print(Fn fn)
{
  SYS_MUTEX_LOCK(forth_mutex);
  std::ostringstream os;
  fn(os);
  std::string s = os.str();
  if (!s.empty() && fout_cb) {
    fout_cb((int)s.size(), s.c_str());
  }
  SYS_MUTEX_UNLOCK(forth_mutex);
}

static void forth_core(std::string idiom)
{
  if (compile) {
    SYS_MUTEX_LOCK(forth_mutex);
    int slot = -1;
    for (auto& d : current_locals) {
      if (d.name == idiom) {
        slot = d.slot;
        break;
      }
    }
    if (slot >= 0) {
      auto c = std::make_shared<Code>(_local_fetch);
      c->set_desc(idiom);
      c->q.push((DU)slot);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
      return;
    }
#if USE_FLOAT
    for (auto& d : current_locals_f) {
      if (d.name == idiom) {
        slot = d.slot;
        break;
      }
    }
    if (slot >= 0) {
      auto c = std::make_shared<Code>(_local_fetch_f);
      c->set_desc(idiom);
      c->q.push((DU)slot);
      last->append(c);
      SYS_MUTEX_UNLOCK(forth_mutex);
      return;
    }
#endif
    SYS_MUTEX_UNLOCK(forth_mutex);
  }

  Code* w;
  SYS_MUTEX_LOCK(forth_mutex);
  w = find(idiom);
  SYS_MUTEX_UNLOCK(forth_mutex);

  if (w) {
    if (compile) {
      if (!w->immd) {
        SYS_MUTEX_LOCK(forth_mutex);
        if (w == compiling_root())
          last->append(unowned_ref(w));
        else
          last->append(w);
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else {
        w->exec();
      }
    }
    else {
      if (w->compile_only) {
        throw std::runtime_error("Interpreting a compile-only word");
      }
      w->exec();
    }
    return;
  }

  if (auto dbl = parse_double(idiom)) {
    if (compile) {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<Lit>(dbl->first));
      last->append(std::make_shared<Lit>(dbl->second));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }
    else {
      current_ctx->ss.push(dbl->first);
      current_ctx->ss.push(dbl->second);
    }
    return;
  }
#if USE_FLOAT
  if (auto fval = parse_float(idiom)) {
    if (compile) {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(std::make_shared<FLit>(*fval));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }
    else {
      fs_push(*fval);
    }
    return;
  }
#endif
  auto n = parse_number(idiom);
  if (!n) throw std::runtime_error("Undefined word");
  if (compile) {
    SYS_MUTEX_LOCK(forth_mutex);
    last->append(std::make_shared<Lit>(*n));
    SYS_MUTEX_UNLOCK(forth_mutex);
  }
  else {
    current_ctx->ss.push(*n);
  }
}

template<typename T>
FV<T>* FV<T>::merge(FV<T>& v)
{
  this->insert(this->end(), v.begin(), v.end());
  v.clear();
  return this;
}

template<typename T>
void FV<T>::push(T n)
{
  this->push_back(n);
}

template<typename T>
T FV<T>::pop()
{
  if (this->empty()) throw std::runtime_error("Stack underflow");
  T n = this->back();
  this->pop_back();
  return n;
}

template<typename T>
T& FV<T>::operator[](int i)
{
  return std::vector<T>::operator[](i < 0 ? (this->size() + i) : i);
}

template<typename T>
const T& FV<T>::operator[](int i) const
{
  return std::vector<T>::operator[](i < 0 ? (this->size() + i) : i);
}

Code::Code(const char* s, const char* d, XT fp, U32 a)
  : name(s),
    desc(d),
    xt(fp),
    attr(a)
{
}

Code::Code(std::string s, bool n)
{
  Code* w = find(s);
  set_name(s);
  desc = "";
  xt = w ? w->xt : nullptr;
  attr = 0;
  token = n ? dict.size() : 0;
  heap_mark = heap_ptr;
  if (n && w) {
    forth_print([&](std::ostringstream& os) { os << "redefined " << s << " "; });
  }
}

Code::Code(XT fp)
  : name(""),
    xt(fp),
    attr(0)
{
}

Code* Code::append(Code* w)
{
  return append(w->shared_from_this());
}

Code* Code::append(std::shared_ptr<Code> w)
{
  pf.push(w);
  return this;
}

void Code::set_name(const std::string& s)
{
  name_buf = std::make_unique<char[]>(s.size() + 1);
  memcpy(name_buf.get(), s.c_str(), s.size() + 1);
  name = name_buf.get();
}

void Code::set_desc(const std::string& s)
{
  desc_buf = std::make_unique<char[]>(s.size() + 1);
  memcpy(desc_buf.get(), s.c_str(), s.size() + 1);
  desc = desc_buf.get();
}

Comment::Comment(const std::string& text, bool dot)
  : Code(_comment)
{
  code_type = CodeType::COMMENT;
  name = dot ? ".(" : "(";
  set_desc(text);
}

Tmp::Tmp()
  : Code(NULL)
{
  code_type = CodeType::TMP;
}

Lit::Lit(DU d)
  : Code(_lit)
{
  code_type = CodeType::LIT;
  q.push(d);
}

#if USE_FLOAT
FLit::FLit(DF v)
  : Code(_flit),
    val(v)
{
  code_type = CodeType::FLIT;
}
#endif

Var::Var(DU d)
  : Code(_var)
{
  code_type = CodeType::VAR;
  q.push(d);
}

Str::Str(std::string s, int tok, int len, bool output)
  : Code(_str)
{
  code_type = CodeType::STR;
  name = output ? ".\"" : "s\"";
  set_desc(s);
  token = (len << 16) | tok;
}

Bran::Bran(XT fp)
  : Code(fp)
{
  code_type = CodeType::BRAN;
  if (fp == _tor2)
    name = "do";
  else if (fp == _loop)
    name = "loop";
  else if (fp == _plus_loop)
    name = "+loop";
  else if (fp == _tor)
    name = ">r";
  else if (fp == _if)
    name = "if";
  else if (fp == _begin)
    name = "begin";
  else if (fp == _does)
    name = "does>";
}
