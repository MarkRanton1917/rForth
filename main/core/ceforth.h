///
/// @file
/// @brief eForth header - C++ vector-based, token-threaded
///
///====================================================================
#pragma once

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include "config.h"

#define CODE(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ }
#define IMMD(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG }

using namespace std;

template<typename T>
struct FV : public vector<T> {
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
    return vector<T>::operator[](i < 0 ? (this->size() + i) : i);
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

string word(char delim = 0);
void ss_dump(DU base);
void see(Code* c);
void words();
void load(const char* fn);
Code* find(string s);

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
  Code(const string s, bool n = true);
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
  Str(string s, int tok = 0, int len = 0)
    : Code(_str)
  {
    name = (new string(s))->c_str();
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

extern void mem_stat();
extern void forth_include(const char* fn);
