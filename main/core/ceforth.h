#pragma once

#include <vector>
#include <string>

typedef uint32_t U32;
typedef int32_t S32;
typedef uint16_t U16;
typedef uint8_t U8;
typedef uintptr_t UFP;
typedef uint16_t IU;
typedef int64_t DU2;
typedef int32_t DU;

template<typename T>
struct FV : public std::vector<T> {
  FV* merge(FV<T>& v);
  void push(T n);
  T pop();
  T& operator[](int i);
};

struct Code;
typedef void (*XT)(Code*);

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
void forth_init();
int forth_vm(const char* cmd, void (*hook)(int, const char*));

extern FV<Code*> dict;

struct Code {
  const static U32 IMMD_FLAG = 0x80000000;
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
      U32 token  :28;
      U32 stage  :2;
      U32 is_str :1;
      U32 immd   :1;
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
