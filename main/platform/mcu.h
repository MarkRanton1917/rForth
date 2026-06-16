#pragma once

#include "ceforth.h"

extern FV<Code*> ops;

void mem_stat();
bool forth_include(const char* fn);

inline void dict_init()
{
  dict.merge(ops);
}
