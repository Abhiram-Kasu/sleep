#include "basic_thread_safe_queue.hpp"
#include "thread_pool.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <latch>
#include <memory>
#include <print>
#include <thread>

namespace {
auto require(bool condition, const char *message) -> void {
  if (!condition) {
    std::println(stderr, "FAILED: {}", message);
    std::exit(EXIT_FAILURE);
  }
}

auto empty_queue_shutdown() -> void {
  using Queue = slp::LockingThreadSafeQueue<size_t>;
  using Worker = decltype([](size_t &) {});
  for (size_t i = 0; i < 100; ++i) {
    auto queue = std::make_shared<Queue>();
    slp::AdaptingThreadPool<Queue, Worker> pool{queue, 4};
    // Destruction must stop workers even if they are polling an empty queue.
  }
}

struct CountedTask {
  std::atomic<unsigned> *visits{};
  std::atomic<size_t> *completed{};
};

auto processes_tasks_once(size_t worker_count) -> void {
  constexpr size_t task_count = 10000;
  std::array<std::atomic<unsigned>, task_count> visits{};
  std::atomic<size_t> completed{0};
  using Queue = slp::LockingThreadSafeQueue<CountedTask>;
  using Worker = decltype([](CountedTask &task) {
    task.visits->fetch_add(1, std::memory_order_relaxed);
    task.completed->fetch_add(1, std::memory_order_release);
  });
  auto queue = std::make_shared<Queue>();
  {
    slp::AdaptingThreadPool<Queue, Worker> pool{queue, worker_count};
    // Workers start with an empty queue, then receive work after startup.
    for (auto &count : visits) {
      queue->push(CountedTask{&count, &completed});
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{5};
    while (completed.load(std::memory_order_acquire) < task_count) {
      require(std::chrono::steady_clock::now() < deadline,
              "workers did not finish all tasks");
      std::this_thread::yield();
    }
  }
  require(queue->is_empty(), "processed queue must be empty");
  for (auto &count : visits) {
    require(count.load(std::memory_order_relaxed) == 1,
            "each task must execute exactly once");
  }
}

struct BlockingTask {
  std::latch *started{};
  std::latch *release{};
  std::atomic<bool> *completed{};
};

auto shutdown_joins_active_task() -> void {
  std::latch started{1};
  std::latch release{1};
  std::atomic<bool> completed{false};
  using Queue = slp::LockingThreadSafeQueue<BlockingTask>;
  using Worker = decltype([](BlockingTask &task) {
    task.started->count_down();
    task.release->wait();
    task.completed->store(true, std::memory_order_release);
  });
  using Pool = slp::AdaptingThreadPool<Queue, Worker>;
  auto queue = std::make_shared<Queue>();
  queue->push(BlockingTask{&started, &release, &completed});
  auto pool = std::make_unique<Pool>(queue, 1);
  started.wait();
  std::jthread releaser{[&release] {
    std::this_thread::sleep_for(std::chrono::milliseconds{25});
    release.count_down();
  }};
  pool.reset();
  require(completed.load(std::memory_order_acquire),
          "pool destruction must join an active task");
}
} // namespace

int main() {
  empty_queue_shutdown();
  processes_tasks_once(4);
  // hardware_concurrency() is allowed to report zero; keep one worker usable.
  processes_tasks_once(0);
  shutdown_joins_active_task();
  std::println("Thread pool regression checks passed");
}
