#pragma once
#include "basic_thread_safe_queue.hpp"
#include "util.hpp"
#include <atomic>
#include <cassert>
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>

namespace slp {
// We need a queue to add tasks to and threads to pull these tasks

class Scheduler {
  // Move only future, only one thread can own and get stuff from the future
  template <typename T> struct Future {
  private:
    std::atomic<bool> m_done_flag{0};
    std::optional<T> m_result;
    friend class Scheduler;
    auto notify_done(T &&res) noexcept -> void {
      SLP_ASSERT(not m_done_flag.load(std::memory_order_acquire));
      m_result.emplace(std::forward<T>(res));
      m_done_flag.store(true, std::memory_order_release);
    }

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

    friend class Scheduler;

    auto notify_done() {
      SLP_ASSERT(not done.load(std::memory_order_acquire));
      done.store(true, std::memory_order_release);
    }

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

private:
  struct ErasedFuture {
    std::function<void(void)> worker;
  };

  using Queue = LockingThreadSafeQueue<ErasedFuture>;

public:
  template <typename Func>
    requires std::invocable<Func>
  auto spawn_blocking(Func &&func) {
    // if there is a return value, we need a place to store it
    //
    using ReturnValue = std::invoke_result<Func>;

    if constexpr (std::is_void_v<ReturnValue>) {
      // Maybe use shared_ptr with weakptr passed to thead_pool?
      auto return_future = Future<ReturnValue>{};

      auto erased_future = ErasedFuture{.worker = [&func, &return_future] {
        return_future.notify_done(func());
      }};

      task_queue.push(erased_future);
      return return_future;

    } else {

      auto return_future = Future<void>{};
      auto erased_future = ErasedFuture{.worker = [&return_future, &func] {
        func();
        return_future.notify_done();
      }};
      task_queue.push(erased_future);
      return return_future;
    }
  }

private:
  Queue task_queue;
};

} // namespace slp
