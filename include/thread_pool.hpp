#pragma once
#include <atomic>
#include <cstddef>
#include <list>
#include <ranges>
#include <thread>
#include <vector>

template <typename Queue, typename WorkerThread, float MaxWaiting = 0.2f,
          /*  Means with N threads running we can
           have N*MaxQueuePressure tasks before
we start summoning more threads */
          float MaxQueuePressure = 1.5f,
          size_t MaxThreads = std::thread::hardware_concurrency()>
  requires(MaxThreads > 2) // One thread will be used to shutdown some threads
                           // if they arent being used, or to spawn more threads
struct AdaptingThreadPool {

private:
  struct ThreadWrapper {
    std::jthread thread_handle;
    std::atomic<bool> is_busy;
  };

  std::atomic<bool> m_alive{true};

  std::atomic<size_t> num_waiting{0};

  std::atomic_flag m_adapter_thread_wakeup{};

  auto worker(std::stop_token stop_token) -> void {}
  // Should only be called from the managing thread since only one thread should
  // be mutating m_threads
  auto summon_thread() -> void {}

  auto manage_threads() -> void {
    while (m_alive.load(std::memory_order_relaxed)) {
      // check how many threads are actually there
      // -1 since adapter thread doesent count
      const auto num_threads = m_threads.size() - 1;
      const auto n_waiting = num_waiting.load(std::memory_order_acquire);

      // how many threads we can keep waiting
      //  for example we might want at most 20% of workers sitting around idle
      const auto idle_ratio = divide_fp(n_waiting, MaxThreads);
      if (idle_ratio > MaxWaiting) {
        // kill some threads to relieve pressure

        if (const auto num_threads_to_kill =
                (idle_ratio - MaxWaiting) * MaxThreads;
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
            // the thread will be detached on init, so this shoulden't block
            // when jthread dtor gets called
            m_threads.erase(iter);
          }
        }

      } else {
        // check if we need to summon more threads
        const auto queue_size = m_queue.size();
        const auto queue_pressure = divide_fp(queue_size, m_threads.size());
        if (queue_pressure > MaxQueuePressure) {
          // summon more threads
          constexpr static auto calc_workers_to_sommon =
              [](size_t NumItems, size_t CurrWorkers, float idealPressure) {
                return (NumItems - (idealPressure * CurrWorkers)) / CurrWorkers;
              };
          const auto num_workers_to_summon =
              std::min(MaxThreads - m_threads.size(),
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

  // First thread is the Adapter (checks whether to spawn more threads, keep
  // current amount, or to kill some threads)
  std::list<ThreadWrapper> m_threads;

  std::shared_ptr<Queue> m_queue;

public:
  AdaptingThreadPool(std::shared_ptr<Queue> queue) {}
};
