#pragma once
#include "basic_thread_safe_queue.hpp"
#include <atomic>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <functional>
#include <semaphore>
#include <type_traits>

namespace slp {
// We need a queue to add tasks to and threads to pull these tasks

class Scheduler {

  struct ErasedTask {
    std::function<void(void)> do_work;
  };
  // Move only future, only one thread can own and get stuff from the future
  template <typename T> struct Future {
  private:
    std::atomic<bool> m_done_flag{0};
    std::optional<T> m_result;

  public:
    // Should only be called by one thread, but we check if the optional has
    // been moved from anyway
    [[nodiscard]] auto try_get() noexcept -> std::optional<T> {
      if (m_done_flag.load(std::memory_order_acquire) and
          m_result.has_value()) {
        return std::move(m_result);
      } else {
        return std::nullopt;
      }
    }
    // Force the user to make this an r value ref to 'consume' the future
    [[nodiscard]] auto get(this Future &&self) noexcept -> T {
      while (not self.m_done_flag.load(std::memory_order_acquire)) {
        self.m_done_flag.wait(true);
      }
      SLP_ASSERT(self.m_result.has_value());
      return std::move(self.m_result.value());
    }
  };
  template <> struct Future<void> {
  private:
    std::atomic<bool> done;

  public:
    auto is_done() const noexcept -> bool {
      return done.load(std::memory_order_relaxed);
    }
    auto wait(this const Future &&self) noexcept -> void {
      while (not self.done.load(std::memory_order_acquire)) {
        self.done.wait(true);
      }
    }
  };

  using Queue = LockingThreadSafeQueue<TaskHandleErased>;

public:
  template <typename Func>
    requires std::invocable<Func>
  auto spawn_blocking(Func &&func) -> void {
    // if there is a return value, we need a place to store it
    //
    using ReturnValue = std::invoke_result<Func>;

    if constexpr (std::is_void_v<ReturnValue>) {

    } else {
    }
  }

private:
  Queue task_queue;
};

} // namespace slp
