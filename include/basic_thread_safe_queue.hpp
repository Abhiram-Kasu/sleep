#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>
#include <queue>
#include <type_traits>
#include <utility>

namespace slp {
// Maybe have a timeout system so that if there is not enough IO bound tasks, we
// can shift threads to cpu bound work?
template <typename T> struct LockingThreadSafeQueue {
public:
  using value_type = T;

private:
  mutable std::mutex m_mutex{};
  std::queue<T> m_queue{};

  alignas(std::hardware_constructive_interference_size)
      std::atomic<std::uint64_t> m_state_token{0};

  auto notify() noexcept -> void {
    m_state_token.fetch_add(1, std::memory_order_relaxed);
    m_state_token.notify_one();
  }

public:
  auto is_empty() const -> bool {
    std::lock_guard lock{m_mutex};
    return m_queue.empty();
  }

  auto size() const -> std::size_t {
    std::lock_guard lock{m_mutex};
    return m_queue.size();
  }

  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  auto push_emplace(Args &&...args) -> void {
    {
      std::lock_guard lock{m_mutex};
      m_queue.emplace(std::forward<Args>(args)...);
    }

    notify();
  }

  template <typename U>
    requires std::is_constructible_v<T, U &&>
  auto push(U &&item) -> void {
    {
      std::lock_guard lock{m_mutex};
      m_queue.emplace(std::forward<U>(item));
    }

    notify();
  }

  auto pop_blocking(T &item) -> void {
    while (true) {
      const auto token = m_state_token.load(std::memory_order_relaxed);

      {
        std::lock_guard lock{m_mutex};

        if (!m_queue.empty()) {
          item = std::move(m_queue.front());
          m_queue.pop();
          return;
        }
      }

      m_state_token.wait(token, std::memory_order_relaxed);
    }
  }

  auto try_pop(T &item) -> bool {
    std::lock_guard lock{m_mutex};

    if (m_queue.empty()) {
      return false;
    }

    item = std::move(m_queue.front());
    m_queue.pop();
    return true;
  }
};

} // namespace slp
