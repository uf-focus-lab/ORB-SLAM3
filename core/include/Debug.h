#pragma once
#include <cstdio> // IWYU pragma: export

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define DEBUG_MSG(...)                                                         \
  {                                                                            \
    printf(__FILE__ ":" STRINGIFY(__LINE__) " " __VA_ARGS__);                  \
    fflush(stdout);                                                            \
  }

#define DEBUG_FN(FN)                                                           \
  DEBUG_MSG(#FN "()\n");                                                       \
  FN
