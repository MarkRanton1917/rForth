// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#pragma once

#include <vector>
#include <string>
#include <cstdint>

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
#define BOOL(f) ((f) ? -1 : 0)

typedef uint32_t U32;
typedef int32_t S32;
typedef uint16_t U16;
typedef uint8_t U8;
typedef uintptr_t UFP;
typedef int64_t DU2;
#if ESP_PLATFORM
typedef int32_t DU;
#elif LINUX_PLATFORM
typedef int64_t DU;
#endif
#if USE_FLOAT
typedef float DF;
#endif

template<typename T>
struct FV : public std::vector<T> {
  FV* merge(FV<T>& v);
  void push(T n);
  T pop();
  T& operator[](int i);
  const T& operator[](int i) const;
};

struct Code;
typedef void (*XT)(Code*);

struct Code {
  const static U32 IMMD_FLAG = 0x80000000;
  const static U32 COMPILE_ONLY_FLAG = 0x40000000;
  const char* name;
  const char* desc;
  XT xt;
  FV<Code*> pf;
  FV<Code*> p1;
  FV<Code*> p2;
  FV<DU> q;
  union {
    U32 attr;
    struct {
      U32 token        :28;
      U32 stage        :2;
      U32 compile_only :1;
      U32 immd         :1;
    };
  };
  Code(const char* s, const char* d, XT fp, U32 a);
  Code(const std::string s, bool n = true);
  Code(XT fp);
  ~Code();
  Code* append(Code* w);
  void exec();
};

struct Comment : Code {
  Comment(const std::string& text, bool dot);
};

struct Tmp : Code {
  Tmp();
};

struct Lit : Code {
  Lit(DU d);
};

#if USE_FLOAT
struct FLit : Code {
  DF val;
  FLit(DF v);
};
#endif

struct Var : Code {
  Var(DU d);
};

struct Str : Code {
  Str(std::string s, int tok = 0, int len = 0, bool print = false);
};

struct Bran : Code {
  Bran(XT fp);
};

struct ForthContext {
  FV<DU> ss;
  FV<DU> rs;
#if USE_FLOAT
  FV<DF> fs;
#endif
  const FV<Code*>* pf;
  size_t ip;
  FV<Code*> call_stack;
  Code* xt;
  bool finished;
  void* handle;
  bool active;
  char pad[PAD_SIZE];
  size_t pad_ptr;
};

void forth_init();
int forth_vm(const char* cmd, void (*hook)(int, const char*));
void dict_add(const Code* words, size_t size);

void ss_push(DU n);
DU ss_pop();
DU alloc_heap(const uint8_t* val, size_t size);
#if USE_FLOAT
void fs_push(DF n);
DF fs_pop();
#endif

// to implement
void mem_stat();
bool forth_include(const char* fn);
