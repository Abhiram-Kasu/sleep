#pragma once
#include <print>
[[noreturn]] inline void assert_failed(const char *expression, const char *file,
                                       int line) {
  std::println(stderr, "{}:{}: assertion failed: %s\n", file, line, expression);
  std::abort();
}

#define SLP_ASSERT(condition)                                                  \
  do {                                                                         \
    if (!(condition)) {                                                        \
      assert_failed(#condition, __FILE__, __LINE__);                           \
    }                                                                          \
  } while (false)

#ifdef NDEBUG
#undef MY_ASSERT
#define MY_ASSERT(condition) ((void)0)
#endif
