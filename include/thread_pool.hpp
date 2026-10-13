#pragma once
#include "util.hpp"
#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <list>
#include <memory>
#include <ranges>
#include <thread>
#include <utility>
#include <vector>
namespace slp{
template <typename Q, typename W, typename TV = Q::value_type>
concept WorkerWorksWithQueue = requires(TV& item) {
  { std::declval<W>()(item) } -> std::same_as<void>;
};

template <typename Queue, typename WorkerFunction, float MaxWaiting = 0.2f,
          /*  Means with N threads running we can
           have N*MaxQueuePressure tasks before
we start summoning more threads */
          float MaxQueuePressure = 1.5f>
  requires(WorkerWorksWithQueue<Queue, WorkerFunction>)
struct AdaptingThreadPool {

private:
  using QueueItemType = Queue::value_type;
  struct ThreadWrapper {
    // Keep the worker's state alive until the thread handle has been destroyed.
    std::atomic<bool> is_busy{false};
    std::jthread thread_handle;
  };

  std::atomic<bool> m_alive{true};

  std::atomic<size_t> num_waiting{0};

  std::atomic_flag m_adapter_thread_wakeup{};

  const size_t m_max_threads;

  auto worker(std::stop_token stop_token, ThreadWrapper &ref) -> void {
    // create instance of WorkerFunction lambda to run
    auto worker_lambda = WorkerFunction{};
    while (m_alive.load(std::memory_order_acquire) and
           not stop_token.stop_requested()) {
      // try to pop item

      num_waiting.fetch_add(1, std::memory_order_relaxed);

      QueueItemType item{};
      while (not m_queue->try_pop(item)) {
        if (stop_token.stop_requested() or
            not m_alive.load(std::memory_order_acquire)) {
          num_waiting.fetch_sub(1, std::memory_order_relaxed);
          return;
        }
        std::this_thread::yield();
      }
      num_waiting.fetch_sub(1, std::memory_order_relaxed);
      ref.is_busy.store(true, std::memory_order_release);
      worker_lambda(item);
      ref.is_busy.store(false, std::memory_order_release);
    }
  }
  // Should only be called from the managing thread since only one thread should
  // be mutating m_threads
  auto summon_thread() -> void {
    // Construct in place: ThreadWrapper contains a non-movable atomic.
    m_threads.emplace_back();
    auto &back = m_threads.back();
    back.thread_handle = std::jthread(
        [this, &back](std::stop_token token) { worker(token, back); });
  }

  auto stop_workers() -> void {
    m_alive.store(false, std::memory_order_release);
    for (auto &item : m_threads) {
      item.thread_handle.request_stop();
    }
    for (auto &item : m_threads) {
      if (item.thread_handle.joinable()) {
        item.thread_handle.join();
      }
    }
    m_threads.clear();
  }

  auto manage_threads() -> void {
    while (m_alive.load(std::memory_order_relaxed)) {
      // check how many threads are actually there
      // -1 since adapter thread doesent count
      const auto num_threads = m_threads.size() - 1;
      const auto n_waiting = num_waiting.load(std::memory_order_acquire);

      // how many threads we can keep waiting
      //  for example we might want at most 20% of workers sitting around idle
      const auto idle_ratio = divide_fp(n_waiting, m_max_threads);
      if (idle_ratio > MaxWaiting) {
        // kill some threads to relieve pressure

        if (const auto num_threads_to_kill =
                (idle_ratio - MaxWaiting) * m_max_threads;
            num_threads_to_kill > 0) {
          auto kill_count{0uz};

          auto items_to_remove =
              std::vector<typename decltype(m_threads)::iterator>();
          items_to_remove.reserve(num_threads_to_kill);
          for (auto it = m_threads.begin(); it != m_threads.end(); ++it) {
            if (not it->is_busy.load(std::memory_order_acquire)) {
              // thread isnt busy, kill it
              it->thread_handle.request_stop();
              kill_count++;
              items_to_remove.push_back(it);
              if (kill_count >= num_threads_to_kill) {
                break;
              }
            }
          }

          for (auto iter : items_to_remove) {
            // The worker still references this wrapper until it has exited.
            iter->thread_handle.join();
            m_threads.erase(iter);
          }
        }

      } else {
        // check if we need to summon more threads
        const auto queue_size = m_queue->size();
        const auto queue_pressure = divide_fp(queue_size, m_threads.size());
        if (queue_pressure > MaxQueuePressure) {
          // summon more threads
          constexpr static auto calc_workers_to_sommon =
              [](size_t NumItems, size_t CurrWorkers, float idealPressure) {
                return (NumItems - (idealPressure * CurrWorkers)) / CurrWorkers;
              };
          const auto num_workers_to_summon =
              std::min(m_max_threads - m_threads.size(),
                       calc_workers_to_sommon(queue_size, m_threads.size(),
                                              MaxQueuePressure));
          if (num_workers_to_summon > 0) {
            for (auto i{0uz}; i < num_workers_to_summon; ++i) {
              summon_thread();
            }
          }
        }
      }
      // wait till someone wakes (preferable a worker thread )
      m_adapter_thread_wakeup.wait(false, std::memory_order_acquire);
    }
  }

  // Stable list nodes hold the state referenced by each worker.
  std::list<ThreadWrapper> m_threads;

  std::shared_ptr<Queue> m_queue;

public:
  AdaptingThreadPool(
      std::shared_ptr<Queue> queue,
      size_t max_threads = std::thread::hardware_concurrency())
      : m_max_threads(std::max(max_threads, size_t{1})),
        m_queue(std::move(queue)) {
    // Start the configured worker count.
    try {
      for(auto i{0uz}; i < m_max_threads; ++i) {
        summon_thread();
      }
    } catch (...) {
      // A failed thread creation must not leave earlier workers using this pool.
      stop_workers();
      throw;
    }
  }

  ~AdaptingThreadPool() {
    stop_workers();
  }
};

}
