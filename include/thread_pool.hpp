#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <print>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

namespace slp {
template <typename Q, typename W, typename TV = Q::value_type>
concept WorkerWorksWithQueue = requires(TV &item) {
  { std::declval<W>()(item) } -> std::same_as<void>;
};

template <typename Queue, typename WorkerFunction, float MaxWaiting = 0.2f,
          float MaxQueuePressure = 1.5f, bool Verbose = false>
  requires(WorkerWorksWithQueue<Queue, WorkerFunction>)
struct AdaptingThreadPool {
private:
  using QueueItemType = Queue::value_type;
  using Clock = std::chrono::steady_clock;
  using ClockRep = Clock::duration::rep;
  static_assert(MaxWaiting >= 0.0f && MaxWaiting < 1.0f);
  static_assert(MaxQueuePressure > 0.0f);

  static constexpr auto idle_grace = std::chrono::milliseconds{250};
  static constexpr auto resize_interval = std::chrono::milliseconds{25};
  static constexpr auto idle_check_interval = std::chrono::milliseconds{50};

  struct ThreadWrapper {
    // State outlives the thread handle during reverse member destruction.
    std::atomic<bool> is_waiting{false};
    std::atomic<ClockRep> idle_since{0};
    std::jthread thread_handle;
  };

  std::atomic<bool> m_alive{true};
  std::atomic<size_t> m_num_waiting{0};
  std::atomic<size_t> m_num_workers{0};
  const size_t m_max_threads;
  std::shared_ptr<Queue> m_queue;

  // Serialize predicate changes with the condition variable's wait operation.
  std::mutex m_adapter_mutex;
  std::condition_variable m_adapter_wakeup;
  std::uint64_t m_adapter_event{0}; // guarded by m_adapter_mutex

  // Only the adapter mutates the list once construction finishes. Destruction
  // joins the adapter before accessing it. Workers hold stable node references.
  std::list<ThreadWrapper> m_threads;
  std::jthread m_adapter_thread;

  auto worker(std::stop_token stop_token, ThreadWrapper &ref) -> void {
    auto worker_lambda = WorkerFunction{};
    while (m_alive.load(std::memory_order_acquire) &&
           !stop_token.stop_requested()) {
      QueueItemType item{};
      bool has_item = m_queue->try_pop(item);
      if (!has_item) {
        ref.idle_since.store(Clock::now().time_since_epoch().count(),
                             std::memory_order_relaxed);
        m_num_waiting.fetch_add(1, std::memory_order_release);
        ref.is_waiting.store(true, std::memory_order_release);
        wake_adapter();

        // Notify on transitions, never on individual polling iterations.
        while (m_alive.load(std::memory_order_acquire) &&
               !stop_token.stop_requested()) {
          if (m_queue->try_pop(item)) {
            has_item = true;
            break;
          }
          std::this_thread::yield();
        }

        ref.is_waiting.store(false, std::memory_order_release);
        m_num_waiting.fetch_sub(1, std::memory_order_release);
        wake_adapter();
        if (!has_item) {
          return;
        }
      }

      worker_lambda(item);
      wake_adapter();
    }
  }

  auto summon_thread() -> void {
    m_threads.emplace_back();
    auto &back = m_threads.back();
    try {
      back.thread_handle = std::jthread(
          [this, &back](std::stop_token token) { worker(token, back); });
    } catch (...) {
      m_threads.pop_back();
      throw;
    }
    m_num_workers.store(m_threads.size(), std::memory_order_release);
  }

  auto rebalance(Clock::time_point now) -> void {
    const auto previous_workers = m_threads.size();
    const auto queue_size = m_queue->size();
    if (queue_size > 0) {
      // Grow to satisfy the queue-pressure target, bounded by the worker cap.
      // Use a comparison rather than mixing floating-point and size_t in min().
      while (m_alive.load(std::memory_order_acquire) &&
             m_threads.size() < m_max_threads &&
             static_cast<double>(queue_size) >
                 static_cast<double>(MaxQueuePressure) * m_threads.size()) {
        try {
          summon_thread();
        } catch (const std::system_error &) {
          // Keep existing workers usable if another thread cannot be created.
          break;
        } catch (const std::bad_alloc &) {
          break;
        }
      }
      if constexpr (Verbose) {
        if (m_threads.size() != previous_workers) {
          std::println("[adapter] grow {} -> {} workers: queued={} (snapshot), "
                       "pressure target={}, cap={}",
                       previous_workers, m_threads.size(), queue_size,
                       MaxQueuePressure, m_max_threads);
        }
      }
      return;
    }

    const auto num_threads = m_threads.size();
    const auto n_waiting =
        std::min(m_num_waiting.load(std::memory_order_acquire), num_threads);
    const auto allowed_waiting =
        static_cast<size_t>(static_cast<double>(MaxWaiting) * num_threads);
    if (num_threads <= 1 || n_waiting <= allowed_waiting) {
      return;
    }
    auto remaining = std::min(n_waiting - allowed_waiting, num_threads - 1);
    for (auto it = m_threads.begin(); it != m_threads.end() && remaining > 0;) {
      if (!m_alive.load(std::memory_order_acquire) || !m_queue->is_empty()) {
        break;
      }
      if (it->is_waiting.load(std::memory_order_acquire) &&
          now - Clock::time_point{Clock::duration{
                    it->idle_since.load(std::memory_order_relaxed)}} >=
              idle_grace) {
        it->thread_handle.request_stop();
        it->thread_handle.join();
        it = m_threads.erase(it);
        m_num_workers.store(m_threads.size(), std::memory_order_release);
        --remaining;
      } else {
        ++it;
      }
    }
    if constexpr (Verbose) {
      if (m_threads.size() != previous_workers) {
        std::println("[adapter] shrink {} -> {} workers: queued=0 (snapshot), "
                     "waiting={} (snapshot), idle grace={} ms",
                     previous_workers, m_threads.size(), n_waiting,
                     idle_grace.count());
      }
    }
  }

  auto manage_threads() -> void {
    auto next_resize = Clock::now();
    while (true) {
      std::unique_lock lock{m_adapter_mutex};
      // Debounce frequent task notifications; shutdown bypasses the cooldown.
      m_adapter_wakeup.wait_until(lock, next_resize, [this] {
        return !m_alive.load(std::memory_order_acquire);
      });
      if (!m_alive.load(std::memory_order_acquire)) {
        return;
      }
      const auto observed = m_adapter_event;
      lock.unlock();

      rebalance(Clock::now());
      next_resize = Clock::now() + resize_interval;

      lock.lock();
      // Events during rebalance are retained by the counter. A timeout lets
      // sustained idleness trigger shrinking even if no further events occur.
      m_adapter_wakeup.wait_for(lock, idle_check_interval, [this, observed] {
        return m_adapter_event != observed ||
               !m_alive.load(std::memory_order_acquire);
      });
    }
  }

  auto stop_workers() -> void {
    m_alive.store(false, std::memory_order_release);
    wake_adapter();
    if (m_adapter_thread.joinable()) {
      m_adapter_thread.join();
    }
    // The adapter can no longer resize the worker list.
    for (auto &item : m_threads) {
      item.thread_handle.request_stop();
    }
    for (auto &item : m_threads) {
      item.thread_handle.join();
    }
    m_threads.clear();
    m_num_workers.store(0, std::memory_order_release);
  }

public:
  AdaptingThreadPool(std::shared_ptr<Queue> queue,
                     size_t max_threads = std::thread::hardware_concurrency())
      : m_max_threads(std::max(max_threads, size_t{1})),
        m_queue(std::move(queue)) {
    if (!m_queue) {
      throw std::invalid_argument{"thread pool requires a queue"};
    }
    try {
      for (size_t i = 0; i < m_max_threads; ++i) {
        summon_thread();
      }
      if constexpr (Verbose) {
        std::println("[pool] started {} workers; limit={}", m_threads.size(),
                     m_max_threads);
      }
      m_adapter_thread = std::jthread([this] { manage_threads(); });
    } catch (...) {
      stop_workers();
      throw;
    }
  }

  ~AdaptingThreadPool() { stop_workers(); }

  // Direct queue users can signal explicitly; submit() does it automatically.
  auto wake_adapter() -> void {
    {
      std::lock_guard lock{m_adapter_mutex};
      ++m_adapter_event;
    }
    m_adapter_wakeup.notify_one();
  }

  template <typename U>
    requires std::constructible_from<QueueItemType, U &&>
  auto submit(U &&item) -> void {
    m_queue->push(std::forward<U>(item));
    wake_adapter();
  }

  template <std::ranges::input_range Range>
    requires std::constructible_from<QueueItemType,
                                     std::ranges::range_reference_t<Range>>
  auto submit_batch(Range &&items) -> void {
    try {
      for (auto &&item : items) {
        m_queue->push(std::forward<decltype(item)>(item));
      }
    } catch (...) {
      wake_adapter(); // Earlier items in a partially submitted batch need work.
      throw;
    }
    wake_adapter();
  }

  [[nodiscard]] auto worker_count() const noexcept -> size_t {
    return m_num_workers.load(std::memory_order_acquire);
  }
};
} // namespace slp
