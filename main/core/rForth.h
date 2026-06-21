#pragma once

#include <vector>
#include <string>
#include <cstdint>

typedef uint32_t U32;
typedef int32_t S32;
typedef uint16_t U16;
typedef uint8_t U8;
typedef uintptr_t UFP;
typedef int64_t DU2;
#if ESP_PLATFORM
typedef int32_t DU;
#elif LINUX
typedef int64_t DU;
#endif

template<typename T>
struct FV : public std::vector<T> {
  FV* merge(FV<T>& v);
  void push(T n);
  T pop();
  T& operator[](int i);
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
      U32 token        :27;
      U32 stage        :2;
      U32 is_str       :1;
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

struct Tmp : Code {
  Tmp();
};

struct Lit : Code {
  Lit(DU d);
};

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
  const std::vector<Code*>* pf;
  size_t ip;
  FV<Code*> call_stack;
  bool finished;
  void* handle;
  bool active;
  char pad[PAD_SIZE];
  size_t pad_ptr;
};

void forth_init();
int forth_vm(const char* cmd, void (*hook)(int, const char*));
extern FV<Code*> dict;
