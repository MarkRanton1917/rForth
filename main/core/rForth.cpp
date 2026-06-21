#include "rForth.h"
#include "mcu.h"

#include <sstream>
#include <cstring>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <atomic>

#if ESP_PLATFORM
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#define MILLIS() (millis())
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
#elif LINUX
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

#define DU0 0
#define DU1 1
#define UINT(v) (static_cast<U32>(v))
#define MOD(m, n) ((m) % (n))
#define ABS(v) (abs(v))
#define ZEQ(v) ((v) == DU0)
#define EQ(a, b) ((a) == (b))
#define LT(a, b) ((a) < (b))
#define GT(a, b) ((a) > (b))
#define RND() (rand())
#define ENDL "\r\n"
#define MARKER_FRAME ((DU)0xDEADBEEF)

#if CASE_SENSITIVE
#define STRCMP(a, b) (strcmp(a, b))
#else
#include <strings.h>
#define STRCMP(a, b) (strcasecmp(a, b))
#endif

#define CODE(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ }
#define IMMD(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG }
#define COMP(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG | Code::COMPILE_ONLY_FLAG }

#define POP()  (current_ctx->ss.pop())
#define PUSH(v) (current_ctx->ss.push(v))

#define BOOL(f) ((f) ? -1 : 0)
#define DICT_PUSH(c) (dict.push(last = (c)))
#define DICT_POP()   (dict.pop(), last = dict[-1])
#define BRAN_TGT()   (dict[-2]->pf[-1])
#define BASE (*(DU*)(&(heap[0])))
#define ALLOT(n) do { heap_ptr += (n); if (heap_ptr > heap.size()) heap.resize(heap_ptr + 1024); } while(0)

void _str(Code* c);
void _lit(Code* c);
void _var(Code* c);
void _tor(Code* c);
void _tor2(Code* c);
void _if(Code* c);
void _begin(Code* c);
void _loop(Code* c);
void _plus_loop(Code* c);
void _abort(Code* c);
void _does(Code* c);

void unnest();
std::string read_word(char delim = 0);
void ss_dump(DU base);
void see(Code* c);
void words();
void load(const char* fn);
Code* find(std::string s);
void output();
template<typename Fn>
void forth_print(Fn fn);
void abort_all_tasks();
void backtrace();

FV<Code*> dict;
std::vector<uint8_t> heap;
size_t heap_ptr = 0;
bool compile = false;
Code* last;

std::istringstream fin;
std::ostringstream fout;
void (*fout_cb)(int, const char*);

static THREAD_LOCAL ForthContext* current_ctx = nullptr;
static std::vector<ForthContext*> all_contexts;
static SYS_MUTEX_TYPE forth_mutex = nullptr;
static std::atomic<bool> abort_requested { false };
static std::string abort_message;
static ForthContext* abort_ctx = nullptr;

void backtrace()
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

void forth_task_entry(void* pvParameters)
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

      Code* w = (*ctx->pf)[ctx->ip++];
      w->exec();
      output();

      if (ctx->handle == nullptr) break;
      if (ctx->finished) break;
      if (abort_requested.load()) break;
    }
  }
  catch (std::exception& e) {
    SYS_MUTEX_LOCK(forth_mutex);
    abort_message = e.what();
    abort_ctx = ctx;
    SYS_MUTEX_UNLOCK(forth_mutex);
    abort_requested.store(true);
    output();
  }
  catch (...) {
    SYS_MUTEX_LOCK(forth_mutex);
    abort_message = "unknown error";
    abort_ctx = ctx;
    SYS_MUTEX_UNLOCK(forth_mutex);
    abort_requested.store(true);
    output();
  }

  output();

  ctx->finished = true;
  ctx->pf = nullptr;
  ctx->ip = 0;
  ctx->handle = nullptr;

#if ESP_PLATFORM
  vTaskDelete(NULL);
#endif
}

void abort_all_tasks()
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
  ctx0->call_stack.clear();
  ctx0->pf = nullptr;
  ctx0->ip = 0;
  ctx0->finished = false;
  ctx0->active = true;
  current_ctx = ctx0;

  abort_message.clear();
  abort_ctx = nullptr;
  abort_requested.store(false);
  SYS_MUTEX_UNLOCK(forth_mutex);
}

void unnest()
{
  ForthContext* ctx = current_ctx;
  bool found = false;
  size_t marker_pos = 0;
  for (int i = (int)ctx->rs.size() - 1; i >= 0; --i) {
    if (ctx->rs[i] == MARKER_FRAME) {
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
  while (ctx->rs.size() > marker_pos + 3)
    ctx->rs.pop_back();
  ctx->ip = (size_t)ctx->rs.back();
  ctx->rs.pop_back();
  ctx->pf = (const std::vector<Code*>*)ctx->rs.back();
  ctx->rs.pop_back();
  ctx->rs.pop_back();
  if (!ctx->call_stack.empty()) ctx->call_stack.pop_back();
}

void Code::exec()
{
  if (xt != nullptr) {
    current_ctx->call_stack.push_back(this);
    xt(this);
    current_ctx->call_stack.pop_back();
  }
  else {
    ForthContext* ctx = current_ctx;
    ctx->call_stack.push_back(this);
    ctx->rs.push_back(MARKER_FRAME);
    ctx->rs.push_back((DU)ctx->pf);
    ctx->rs.push_back((DU)ctx->ip);
    ctx->pf = &pf;
    ctx->ip = 0;
    while (ctx->pf == &pf && ctx->ip < pf.size()) {
      Code* w = pf[ctx->ip++];
      w->exec();
      if (abort_requested.load()) {
        SYS_MUTEX_LOCK(forth_mutex);
        std::string msg = abort_message;
        SYS_MUTEX_UNLOCK(forth_mutex);
        throw std::runtime_error(msg);
      }
    }
    if (ctx->pf == &pf) unnest();
    if (!ctx->call_stack.empty() && ctx->call_stack.back() == this) ctx->call_stack.pop_back();
  }
}

const Code rom[] = { CODE("bye", exit(0)),
  CODE("+",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a + b);
    }),
  CODE("-",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a - b);
    }),
  CODE("*",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a * b);
    }),
  CODE("/",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a / b);
    }),
  CODE("mod",
    {
      DU b = POP();
      DU a = POP();
      PUSH(MOD(a, b));
    }),
  CODE("*/",
    {
      DU b = POP();
      DU a = POP();
      DU c = POP();
      PUSH(a * b / c);
    }),
  CODE("/mod",
    {
      DU b = POP();
      DU a = POP();
      DU m = MOD(a, b);
      PUSH(m);
      PUSH(a / b);
    }),
  CODE("*/mod",
    {
      DU b = POP();
      DU a = POP();
      DU c = POP();
      DU2 n = (DU2)a * b;
      DU2 m = MOD(n, c);
      PUSH((DU)m);
      PUSH((DU)(n / c));
    }),
  CODE("and",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) & UINT(b));
    }),
  CODE("or",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) | UINT(b));
    }),
  CODE("xor",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) ^ UINT(b));
    }),
  CODE("abs",
    {
      DU a = POP();
      PUSH(ABS(a));
    }),
  CODE("negate",
    {
      DU a = POP();
      PUSH(-a);
    }),
  CODE("invert",
    {
      DU a = POP();
      PUSH(~UINT(a));
    }),
  CODE("rshift",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) >> UINT(b));
    }),
  CODE("lshift",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) << UINT(b));
    }),
  CODE("max",
    {
      DU b = POP();
      DU a = POP();
      PUSH((a > b) ? a : b);
    }),
  CODE("min",
    {
      DU b = POP();
      DU a = POP();
      PUSH((a < b) ? a : b);
    }),
  CODE("2*",
    {
      DU a = POP();
      PUSH(a * 2);
    }),
  CODE("2/",
    {
      DU a = POP();
      PUSH(a / 2);
    }),
  CODE("1+",
    {
      DU a = POP();
      PUSH(a + 1);
    }),
  CODE("1-",
    {
      DU a = POP();
      PUSH(a - 1);
    }),
  CODE("0=",
    {
      DU a = POP();
      PUSH(BOOL(ZEQ(a)));
    }),
  CODE("0<",
    {
      DU a = POP();
      PUSH(BOOL(LT(a, DU0)));
    }),
  CODE("0>",
    {
      DU a = POP();
      PUSH(BOOL(GT(a, DU0)));
    }),
  CODE("=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(EQ(a, b)));
    }),
  CODE(">",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(GT(a, b)));
    }),
  CODE("<",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(LT(a, b)));
    }),
  CODE("<>",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!EQ(a, b)));
    }),
  CODE(">=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!LT(a, b)));
    }),
  CODE("<=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!GT(a, b)));
    }),
  CODE("u<",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(UINT(a) < UINT(b)));
    }),
  CODE("u>",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(UINT(a) > UINT(b)));
    }),
  CODE("dup",
    {
      DU a = POP();
      PUSH(a);
      PUSH(a);
    }),
  CODE("drop", { POP(); }),
  CODE("swap",
    {
      DU a = POP();
      DU b = POP();
      PUSH(a);
      PUSH(b);
    }),
  CODE("over",
    {
      DU a = POP();
      DU b = POP();
      PUSH(b);
      PUSH(a);
      PUSH(b);
    }),
  CODE("rot",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      PUSH(b);
      PUSH(a);
      PUSH(c);
    }),
  CODE("pick",
    {
      DU n = POP();
      if (n < 0 || n >= (DU)current_ctx->ss.size()) throw std::runtime_error("pick out of range");
      DU v = current_ctx->ss[current_ctx->ss.size() - 1 - n];
      PUSH(v);
    }),
  CODE("nip",
    {
      DU a = POP();
      POP();
      PUSH(a);
    }),
  CODE("-rot",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      PUSH(a);
      PUSH(c);
      PUSH(b);
    }),
  CODE("tuck",
    {
      DU a = POP();
      DU b = POP();
      PUSH(a);
      PUSH(b);
      PUSH(a);
    }),
  CODE("?dup",
    {
      DU a = POP();
      if (a != DU0) {
        PUSH(a);
        PUSH(a);
      }
      else
        PUSH(a);
    }),
  CODE("2dup",
    {
      DU a = POP();
      DU b = POP();
      PUSH(b);
      PUSH(a);
      PUSH(b);
      PUSH(a);
    }),
  CODE("2drop",
    {
      POP();
      POP();
    }),
  CODE("2swap",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      DU d = POP();
      PUSH(c);
      PUSH(d);
      PUSH(a);
      PUSH(b);
    }),
  CODE("2over",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      DU d = POP();
      PUSH(c);
      PUSH(d);
      PUSH(a);
      PUSH(b);
      PUSH(c);
      PUSH(d);
    }),
  CODE(">r", current_ctx->rs.push(POP())), CODE("r>", PUSH(current_ctx->rs.pop())),
  CODE("r@", PUSH(current_ctx->rs[-1])), CODE("base", PUSH(BASE)),
  CODE("decimal",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      fout << std::setbase(BASE = 10);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("hex",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      fout << std::setbase(BASE = 16);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("bl", PUSH(0x20)), CODE("cr", { forth_print([&](std::ostringstream& os) { os << ENDL; }); }),
  CODE(".",
    {
      DU v = POP();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << v << " "; });
    }),
  CODE(".r",
    {
      DU w = POP();
      DU v = POP();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << std::setw(w) << v; });
    }),
  CODE("u.r",
    {
      DU w = POP();
      DU v = POP();
      forth_print([&](std::ostringstream& os) { os << std::setbase(BASE) << std::setw(w) << ABS(v); });
    }),
  CODE("key", PUSH(read_word()[0])),
  CODE("emit",
    {
      char ch = (char)POP();
      forth_print([&](std::ostringstream& os) { os << ch; });
    }),
  CODE("space", { forth_print([&](std::ostringstream& os) { os << ' '; }); }),
  CODE("spaces",
    {
      DU n = POP();
      forth_print([&](std::ostringstream& os) { os << std::setw(n) << ""; });
    }),
  CODE("type",
    {
      DU len = POP();
      const char* addr = reinterpret_cast<const char*>(POP());
      forth_print([&](std::ostringstream& os) {
        for (DU i = 0; i < len; ++i)
          os << addr[i];
      });
    }),
  IMMD(".(",
    {
      std::string s = read_word(')');
      forth_print([&](std::ostringstream& os) { os << s; });
    }),
  IMMD(".(",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      fout << read_word(')');
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  IMMD("\\",
    {
      std::string s;
      getline(fin, s, '\n');
    }),
  IMMD(".\"",
    {
      std::string s = read_word('"').substr(1);
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(new Str(s, last->token, last->pf.size(), true));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  IMMD("s\"",
    {
      std::string s = read_word('"').substr(1);
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(new Str(s, last->token, last->pf.size(), false));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else {
        int len = s.length();
        size_t copy_len = std::min(PAD_SIZE - 1, len);
        current_ctx->pad_ptr = PAD_SIZE - 1 - len;
        char* addr = &current_ctx->pad[current_ctx->pad_ptr];
        memcpy(addr, s.c_str(), copy_len);
        current_ctx->pad[PAD_SIZE - 1] = '\0';
        PUSH((DU)addr);
        PUSH(len);
      }
    }),
  IMMD("abort\"",
    {
      std::string s = read_word('"').substr(1);
      if (compile) {
        Code* c = new Code((new std::string(s))->c_str(), "", _abort, 0);
        c->is_str = true;
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
      DU n = POP();
      DU base = BASE;
      DU digit = n % base;
      n /= base;
      if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
      char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
      current_ctx->pad[--current_ctx->pad_ptr] = ch;
      PUSH(n);
    }),
  CODE("#s",
    {
      DU n = POP();
      DU base = BASE;
      while (n != 0) {
        DU digit = n % base;
        n /= base;
        if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
        char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
        current_ctx->pad[--current_ctx->pad_ptr] = ch;
      }
      PUSH(n);
    }),
  CODE("#>",
    {
      DU n = POP();
      (void)n;
      PUSH((DU)(current_ctx->pad + current_ctx->pad_ptr));
      PUSH(PAD_SIZE - current_ctx->pad_ptr - 1);
    }),
  CODE("hold",
    {
      char ch = (char)POP();
      if (current_ctx->pad_ptr == 0) throw std::runtime_error("PAD overflow");
      current_ctx->pad[--current_ctx->pad_ptr] = ch;
    }),
  COMP("if",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(new Bran(_if));
      DICT_PUSH(new Tmp());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("else",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      b->stage = 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("then",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      int s = b->stage;
      if (s == 0) {
        b->pf.merge(last->pf);
        DICT_POP();
      }
      else {
        b->p1.merge(last->pf);
        if (s == 1) DICT_POP();
      }
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("begin",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(new Bran(_begin));
      DICT_PUSH(new Tmp());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("while",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      b->stage = 2;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("repeat",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->p1.merge(last->pf);
      DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("again",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      DICT_POP();
      b->stage = 1;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("until",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("do",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(new Bran(_tor2));
      last->append(new Bran(nullptr));
      DICT_PUSH(new Tmp());
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("i", PUSH(current_ctx->rs[-1])),
  COMP("leave",
    {
      current_ctx->rs.pop();
      current_ctx->rs.pop();
      unnest();
    }),
  COMP("loop",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->xt = _loop;
      b->name = "loop";
      b->pf.merge(last->pf);
      DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("+loop",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      Code* b = BRAN_TGT();
      b->xt = _plus_loop;
      b->name = "+loop";
      b->pf.merge(last->pf);
      DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("exit", { unnest(); }), CODE("[", { compile = false; }), CODE("]", { compile = true; }),
  CODE(":",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DICT_PUSH(new Code(read_word()));
      compile = true;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP(";",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      compile = false;
      Code* exit_word = find("exit");
      if (exit_word) last->append(exit_word);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("constant",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DICT_PUSH(new Code(read_word()));
      DU v = POP();
      last->append(new Lit(v));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("variable",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DICT_PUSH(new Code(read_word()));
      DU addr = (DU)&heap[heap_ptr];
      ALLOT(sizeof(DU));
      *(DU*)addr = 0;
      last->append(new Var(addr));
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
      Code* w = reinterpret_cast<Code*>(POP());
      w->exec();
    }),
  CODE("create",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DICT_PUSH(new Code(read_word()));
      last->append(new Var((DU)&heap[heap_ptr]));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  COMP("does>",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      last->append(new Bran(_does));
      last->pf[-1]->token = last->token;
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("@",
    {
      DU addr = POP();
      PUSH(*(DU*)addr);
    }),
  CODE("!",
    {
      DU addr = POP();
      DU val = POP();
      *(DU*)addr = val;
    }),
  CODE("c@",
    {
      DU addr = POP();
      PUSH(*(char*)addr);
    }),
  CODE("c!",
    {
      DU addr = POP();
      DU val = POP();
      *(char*)addr = (char)(val & 0xFF);
    }),
  CODE("+!",
    {
      DU addr = POP();
      DU val = POP();
      *(DU*)addr += val;
    }),
  CODE("?",
    {
      DU addr = POP();
      forth_print([&](std::ostringstream& os) { os << *(DU*)addr << " "; });
    }),
  CODE(",",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DU val = POP();
      *(DU*)&heap[heap_ptr] = val;
      ALLOT(sizeof(DU));
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("cells",
    {
      DU val = POP();
      PUSH(val * sizeof(DU));
    }),
  CODE("allot",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      DU n = POP();
      ALLOT(n);
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("here", PUSH((DU)&heap[heap_ptr])),
  CODE("'",
    {
      std::string s = read_word();
      Code* w = find(s);
      if (w)
        PUSH(reinterpret_cast<DU>(w));
      else
        throw std::runtime_error("Undefined word");
    }),
  COMP("[']",
    {
      std::string s = read_word();
      Code* w = find(s);
      if (!w) throw std::runtime_error("Undefined word");
      if (compile) {
        SYS_MUTEX_LOCK(forth_mutex);
        last->append(new Lit((DU)w));
        SYS_MUTEX_UNLOCK(forth_mutex);
      }
      else
        PUSH((DU)w);
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
  CODE("depth", PUSH(current_ctx->ss.size())), CODE("mstat", mem_stat()), CODE("ms", PUSH(MILLIS())),
  CODE("rnd", PUSH(RND())),
  CODE("included",
    {
      size_t len = (size_t)(POP());
      (void)len;
      const char* filename = reinterpret_cast<const char*>(static_cast<uintptr_t>(POP()));
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
      int t = std::max((int)w->token, find("boot")->token + 1);
      for (int i = dict.size(); i > t; i--)
        DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("boot",
    {
      SYS_MUTEX_LOCK(forth_mutex);
      int t = find("boot")->token + 1;
      for (int i = dict.size(); i > t; i--)
        DICT_POP();
      SYS_MUTEX_UNLOCK(forth_mutex);
    }),
  CODE("pause", { SYS_SLEEP_MS(1); }),
  CODE("delay",
    {
      DU ms = POP();
      if (ms > 0) SYS_SLEEP_MS(ms);
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
      DU xt_addr = POP();
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
      PUSH((DU)idx);
    }),
  CODE("resume",
    {
      DU id = POP();
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
  CODE("active?", {
    DU id = POP();
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
    PUSH(BOOL(active));
  }) };

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
  name = (new std::string(s))->c_str();
  desc = "";
  xt = w ? w->xt : nullptr;
  token = n ? dict.size() : 0;
  if (n && w) {
    SYS_MUTEX_LOCK(forth_mutex);
    fout << "redefined " << s << " ";
    SYS_MUTEX_UNLOCK(forth_mutex);
  }
}
Code::Code(XT fp)
  : name(""),
    xt(fp),
    attr(0)
{
}
Code::~Code()
{
}
Code* Code::append(Code* w)
{
  pf.push(w);
  return this;
}

Tmp::Tmp()
  : Code(NULL)
{
}
Lit::Lit(DU d)
  : Code(_lit)
{
  q.push(d);
}
Var::Var(DU d)
  : Code(_var)
{
  q.push(d);
}
Str::Str(std::string s, int tok, int len, bool output)
  : Code(_str)
{
  name = (new std::string(s))->c_str();
  token = (len << 16) | tok;
  is_str = !output;
}
Bran::Bran(XT fp)
  : Code(fp)
{
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
  else
    name = "";
  is_str = 0;
}

void _does(Code* c)
{
  bool hit = false;
  SYS_MUTEX_LOCK(forth_mutex);
  for (Code* w : dict[c->token]->pf) {
    if (hit) last->append(w);
    if (STRCMP(w->name, "does>") == 0) hit = true;
  }
  SYS_MUTEX_UNLOCK(forth_mutex);
  throw 0;
}
void _str(Code* c)
{
  if (c->is_str) {
    PUSH((DU)c->name);
    PUSH(strlen(c->name));
  }
  else {
    forth_print([&](std::ostringstream& os) { os << c->name; });
  }
}
void _lit(Code* c)
{
  PUSH(c->q[0]);
}
void _var(Code* c)
{
  PUSH(c->q[0]);
}
void _tor(Code* c)
{
  current_ctx->rs.push(POP());
}
void _tor2(Code* c)
{
  DU first = POP();
  DU limit = POP();
  current_ctx->rs.push(limit);
  current_ctx->rs.push(first);
}
void _if(Code* c)
{
  if (POP()) {
    for (Code* w : c->pf)
      w->exec();
  }
  else {
    for (Code* w : c->p1)
      w->exec();
  }
}
void _begin(Code* c)
{
  int b = c->stage;
  while (true) {
    for (Code* w : c->pf)
      w->exec();
    if (b == 0 && POP() != 0) break;
    if (b == 1) continue;
    if (b == 2 && POP() == 0) break;
    for (Code* w : c->p1)
      w->exec();
  }
}
void _loop(Code* c)
{
  try {
    while (true) {
      for (Code* w : c->pf)
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
void _plus_loop(Code* c)
{
  try {
    while (true) {
      for (Code* w : c->pf)
        w->exec();
      if (current_ctx->rs.size() < 2) break;
      DU n = POP();
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
void _abort(Code* c)
{
  SYS_MUTEX_LOCK(forth_mutex);
  abort_message = c->name ? c->name : "Aborted";
  abort_ctx = current_ctx;
  SYS_MUTEX_UNLOCK(forth_mutex);
  abort_requested.store(true);
  throw std::runtime_error(abort_message);
}

std::string read_word(char delim)
{
  std::string s;
  delim ? getline(fin, s, delim) : fin >> s;
  return s;
}
void ss_dump(DU base)
{
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
  fout << "<" << current_ctx->ss.size() << "> ";
  for (DU v : current_ctx->ss)
    fout << rdx(v, base) << ' ';
}
void _see(Code* c)
{
  if (!c) return;
  if (c->xt == _lit) {
    fout << c->q[0] << " ";
    return;
  }
  const char* nm = c->name ? c->name : "";
  if (strcmp(nm, "if") == 0) {
    fout << "if ";
    for (Code* w : c->pf)
      _see(w);
    if (c->stage == 1 && !c->p1.empty()) {
      fout << "else ";
      for (Code* w : c->p1)
        _see(w);
    }
    fout << "then ";
    return;
  }
  if (strcmp(nm, "begin") == 0) {
    fout << "begin ";
    for (Code* w : c->pf)
      _see(w);
    if (c->stage == 2) {
      fout << "while ";
      for (Code* w : c->p1)
        _see(w);
      fout << "repeat ";
    }
    else if (c->stage == 0)
      fout << "until ";
    else if (c->stage == 1)
      fout << "again ";
    return;
  }
  if (strcmp(nm, "do") == 0) {
    fout << "do ";
    return;
  }
  if (strcmp(nm, "loop") == 0 || strcmp(nm, "+loop") == 0) {
    for (Code* w : c->pf)
      _see(w);
    fout << nm << " ";
    return;
  }
  if (nm[0] != '\0' && nm[0] != '\t') fout << nm << " ";
}
void see(Code* c)
{
  if (!c) {
    fout << "  -> { not found }";
    return;
  }
  if (c->xt) {
    fout << "  ->{ " << c->desc << "; } ";
    return;
  }
  fout << ": " << c->name << " ";
  for (Code* w : c->pf)
    _see(w);
  fout << "; ";
}
void words()
{
  const int WIDTH = 60;
  int x = 0;
  fout << std::setbase(16) << std::setfill('0');
  for (Code* w : dict) {
#if CC_DEBUG > 1
    fout << std::setw(4) << w->token << "> " << (UFP)w << ' ' << std::setw(8) << (U32)(UFP)w->xt
         << (w->is_str ? '"' : ':') << (w->immd ? '*' : ' ') << w->name << "  " << ENDL;
#else
    fout << "  " << w->name;
    x += (strlen(w->name) + 2);
    if (x > WIDTH) {
      fout << ENDL;
      x = 0;
    }
#endif
  }
  fout << std::setfill(' ') << std::setbase(BASE) << ENDL;
}
void load(const char* fn)
{
  void (*cb)(int, const char*) = fout_cb;
  std::string in;
  getline(fin, in);
  if (!forth_include(fn)) throw std::runtime_error("Can't open file");
  fout_cb = cb;
  fin.clear();
  fin.str(in);
}
Code* find(std::string s)
{
  for (int i = dict.size() - 1; i >= 0; --i)
    if (STRCMP(s.c_str(), dict[i]->name) == 0) return dict[i];
  return nullptr;
}
DU parse_number(std::string idiom)
{
  const char* cs = idiom.c_str();
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
#if DU == float
  DU n = (b == 10) ? strtof(cs, &p) : strtol(cs, &p, b);
#else
  DU n = strtol(cs, &p, b);
#endif
  if (errno || *p != '\0') throw std::runtime_error("Undefined word");
  return n;
}

void output()
{
  if (!fout_cb) return;

  std::string out;
  SYS_MUTEX_LOCK(forth_mutex);
  out = fout.str();
  if (!out.empty()) {
    fout.str("");
    fout.clear();
  }
  SYS_MUTEX_UNLOCK(forth_mutex);

  if (!out.empty()) {
    fout_cb((int)out.size(), out.c_str());
  }
}

template<typename Fn>
void forth_print(Fn fn)
{
  SYS_MUTEX_LOCK(forth_mutex);
  fn(fout);
  SYS_MUTEX_UNLOCK(forth_mutex);
  output();
}

void forth_init()
{
  const int sz = sizeof(rom) / sizeof(Code);
  dict.reserve(sz * 2);
  for (int i = 0; i < sz; ++i)
    DICT_PUSH((Code*)&rom[i]);
  heap.resize(HEAP_SIZE);
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

void forth_core(std::string idiom)
{
  Code* w;
  SYS_MUTEX_LOCK(forth_mutex);
  w = find(idiom);
  SYS_MUTEX_UNLOCK(forth_mutex);

  if (w) {
    if (compile) {
      if (!w->immd) {
        SYS_MUTEX_LOCK(forth_mutex);
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

  DU n = parse_number(idiom);
  if (compile) {
    SYS_MUTEX_LOCK(forth_mutex);
    last->append(new Lit(n));
    SYS_MUTEX_UNLOCK(forth_mutex);
  }
  else {
    current_ctx->ss.push(n);
  }
}

int forth_vm(const char* cmd, void (*hook)(int, const char*))
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
    if (exception_what != "Undefined word") {
      size_t pos = line.find(idiom);
      if (pos != std::string::npos)
        ret.replace(pos, idiom.length(), ">>>" + idiom + "<<<");
      else
        ret = ">>>" + idiom + "<<<";
    }
    else if (idiom == "see") {
      size_t pos = line.find("see");
      if (pos != std::string::npos) {
        std::string nxt = next_token(line, pos + 3);
        if (!nxt.empty()) {
          size_t nxt_pos = line.find(nxt, pos + 3);
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
    else {
      size_t pos = line.find(idiom);
      if (pos != std::string::npos)
        ret.replace(pos, idiom.length(), ">>>" + idiom + "<<<");
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
        output();
      }
      catch (std::exception& e) {
        forth_print([&](std::ostringstream& os) {
          os << ENDL;
          os << ":" << ++err_cnt << ": " << e.what() << ENDL;
          std::string marked_line = replace(current_line, idiom, e.what());
          os << marked_line << ENDL;
        });
        backtrace();
        error_occured = true;
        current_ctx->ss.clear();
        current_ctx->rs.clear();
        current_ctx->call_stack.clear();
        current_ctx->pf = nullptr;
        current_ctx->ip = 0;
        compile = false;
        getline(fin, idiom, '\n');
        abort_all_tasks();
        return;
      }
      catch (int) {
        output();
      }
    }
  };

#if ESP_PLATFORM
  if (!hook) {
    fout_cb = [](int len, const char* s) {
      (void)len;
      Serial.print(s);
    };
  }
  else {
    fout_cb = hook;
  }
#else
  auto cb = [](int, const char* rst) { std::cout << rst; };
  fout_cb = hook ? hook : cb;
#endif

  std::istringstream istm(cmd);
  std::string line;
  fout.str("");

  while (getline(istm, line)) {
    fin.clear();
    fin.str(line);
    outer(line);
    if (abort_requested.load()) {
      forth_print([&](std::ostringstream& os) {
        os << ENDL;
        os << ":" << ++err_cnt << ": " << abort_message << ENDL;
        std::string marked_line = replace(line, idiom, abort_message);
        os << marked_line << ENDL;
      });
      backtrace();
      error_occured = true;
      abort_all_tasks();
      break;
    }
  }
  if (!error_occured) {
    SYS_MUTEX_LOCK(forth_mutex);
    if (compile)
      fout << " compiled" << ENDL;
    else
      fout << " ok" << ENDL;
    SYS_MUTEX_UNLOCK(forth_mutex);
  }
  output();
  return 0;
}
