#pragma once
#include <atomic>
#include <cstddef>
#include <list>
#include <thread>

template <typename Queue, typename WorkerThread, float MaxLoadFactor = 0.5f,
          size_t MaxThreads = std::thread::hardware_concurrency()>
  requires(MaxThreads > 2) // One thread will be used to shutdown some threads
                           // if they arent being used, or to spawn more threads
struct AdaptingThreadPool {

private:
  std::atomic<bool> m_alive{true};

  std::atomic<size_t> num_working{0};
  std::atomic<size_t> num_waiting{0};
  auto manage_threads() {
    while (m_alive.load(std::memory_order_relaxed)) {
    }
  }

  // First thread is the Adapter (checks whether to spawn more threads, keep
  // current amount, or to kill some threads)
  std::list<std::jthread> m_threads;

  std::shared_ptr<Queue> m_queue;

public:
  AdaptingThreadPool(std::shared_ptr<Queue> queue) {}
};
