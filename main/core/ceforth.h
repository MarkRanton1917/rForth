#pragma once

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

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

#if CASE_SENSITIVE
#define STRCMP(a, b) (strcmp(a, b))
#else
#include <strings.h>
#define STRCMP(a, b) (strcasecmp(a, b))
#endif

#define DO_WASM __EMSCRIPTEN__

#if (ARDUINO || ESP32)
#include <Arduino.h>
#define to_string(i) string(String(i).c_str())
#if ESP32
#define analogWrite(c, v, mx) ledcWrite((c), (8191 / mx) * min((int)(v), mx))
#endif

#elif DO_WASM
#include <emscripten.h>
#define millis() EM_ASM_INT({ return Date.now(); })
#define delay(ms)                                                              \
    EM_ASM({ let t = setTimeout(() = > clearTimeout(t), $0); }, ms)
#define yield()
#else
#include <chrono>
#include <thread>
#define millis()                                                               \
    chrono::duration_cast<chrono::milliseconds>(                               \
        chrono::steady_clock::now().time_since_epoch())                        \
        .count()
#define delay(ms) this_thread::sleep_for(chrono::milliseconds(ms))
#define yield() this_thread::yield()
#define PROGMEM
#endif

#define CODE(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ }
#define IMMD(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG }
#define POP()  (ss.pop())
#define PUSH(v) (ss.push(v))
#define BOOL(f) ((f) ? -1 : 0)
#define VAR(i_w) (*(dict[(int)((UINT(i_w)) & 0xffff)]->pf[0]->q.data() + ((UINT(i_w)) >> 16)))
#define DICT_PUSH(c) (dict.push(last = (c)))
#define DICT_POP()   (dict.pop(), last = dict[-1])
#define BRAN_TGT()   (dict[-2]->pf[-1])
#define BASE (VAR(0))
#define UNNEST() throw 0

typedef uint32_t U32; ///< unsigned 32-bit integer
typedef int32_t S32; ///< signed 32-bit integer
typedef uint16_t U16; ///< unsigned 16-bit integer
typedef uint8_t U8; ///< byte, unsigned character
typedef uintptr_t UFP; ///< function pointer as integer
typedef uint16_t IU; ///< instruction pointer unit
typedef int64_t DU2;
typedef int32_t DU;

template<typename T>
struct FV : public std::vector<T> {
  FV* merge(FV<T>& v)
  {
    this->insert(this->end(), v.begin(), v.end());
    v.clear();
    return this;
  }
  void push(T n)
  {
    this->push_back(n);
  }
  T pop()
  {
    if (this->empty()) {
      throw std::runtime_error("Stack underflow");
    }
    T n = this->back();
    this->pop_back();
    return n;
  }
  T& operator[](int i)
  {
#if CC_DEBUG
    return this->at(i < 0 ? (this->size() + i) : i);
#else
    return std::vector<T>::operator[](i < 0 ? (this->size() + i) : i);
#endif
  }
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
void _for(Code* c);
void _loop(Code* c);
void _plus_loop(Code* c);
void _does(Code* c);

std::string word(char delim = 0);
void ss_dump(DU base);
void see(Code* c);
void words();
void load(const char* fn);
Code* find(std::string s);

void forth_init();
int forth_vm(const char* cmd, void (*hook)(int, const char*));

extern FV<Code*> dict;
extern FV<DU> ss;

struct Code {
  const static U32 IMMD_FLAG = 0x80000000;
  const char* name;
  const char* desc;
  XT xt = NULL;
  FV<Code*> pf;
  FV<Code*> p1;
  FV<Code*> p2;
  FV<DU> q;
  union {
    U32 attr = 0;
    struct {
      U32 token  :28;
      U32 stage  :2;
      U32 is_str :1;
      U32 immd   :1;
    };
  };
  Code(const char* s, const char* d, XT fp, U32 a);
  Code(const std::string s, bool n = true);
  Code(XT fp)
    : name(""),
      xt(fp),
      attr(0)
  {
  }
  ~Code()
  {
  }

  Code* append(Code* w)
  {
    pf.push(w);
    return this;
  }
  void exec()
  {
    if (xt) {
      xt(this);
      return;
    }
    for (Code* w : pf) {
      w->exec();
    }
  }
};

struct Tmp : Code {
  Tmp()
    : Code(NULL)
  {
  }
};

struct Lit : Code {
  Lit(DU d)
    : Code(_lit)
  {
    q.push(d);
  }
};

struct Var : Code {
  Var(DU d)
    : Code(_var)
  {
    q.push(d);
  }
};

struct Str : Code {
  Str(std::string s, int tok = 0, int len = 0)
    : Code(_str)
  {
    name = (new std::string(s))->c_str();
    token = (len << 16) | tok;
    is_str = 1;
  }
};

struct Bran : Code {
  Bran(XT fp)
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
    else if (fp == _for)
      name = "for";
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
};
