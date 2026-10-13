#pragma once
#include <print>
#include <type_traits>
#include <utility>
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

#include <concepts>
#include <utility>

template <typename T, typename C>
concept is_statically_convertible = requires(T item) {
    static_cast<C>(item);
};
template <typename ResultType = float, typename... Args>
requires ((is_statically_convertible<Args, ResultType> or std::convertible_to<Args, ResultType>) && ...)
[[nodiscard]] constexpr auto divide(Args &&...params) -> ResultType
{
    // The compiler guarantees at least one cast method works
    return (static_cast<ResultType>(std::forward<Args>(params)) / ...);
}
