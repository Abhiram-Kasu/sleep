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
#include <vector>

namespace {
auto require(bool condition, const char *message) -> void {
  if (!condition) {
    std::println(stderr, "FAILED: {}", message);
    std::exit(EXIT_FAILURE);
  }
}

template <typename Predicate>
auto wait_until(Predicate predicate, const char *message) -> void {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
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
      pool.submit(CountedTask{&count, &completed});
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{5};
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

struct BurstTask {
  std::atomic<size_t> *started{};
  std::latch *release{};
  std::atomic<size_t> *completed{};
};

auto shrinks_and_regrows() -> void {
  using Queue = slp::LockingThreadSafeQueue<BurstTask>;
  using Worker = decltype([](BurstTask &task) {
    task.started->fetch_add(1, std::memory_order_release);
    task.release->wait();
    task.completed->fetch_add(1, std::memory_order_release);
  });
  auto queue = std::make_shared<Queue>();
  slp::AdaptingThreadPool<Queue, Worker> pool{queue, 4};

  // No new events occur once all workers are idle. Timed checks must prune
  // them.
  wait_until([&] { return pool.worker_count() == 1; },
             "idle pool did not shrink to one worker");

  // Hold callbacks open so growth is observable and cannot be hidden by a
  // worker draining the whole burst before the adapter gets scheduled.
  for (size_t cycle = 0; cycle < 3; ++cycle) {
    std::atomic<size_t> started{0};
    std::atomic<size_t> completed{0};
    std::latch release{1};
    std::array<BurstTask, 12> tasks;
    for (auto &task : tasks) {
      task = BurstTask{&started, &release, &completed};
    }
    if (cycle == 0) {
      pool.submit_batch(tasks);
    } else if (cycle == 1) {
      for (auto &task : tasks) {
        pool.submit(task);
      }
    } else {
      // Legacy users can push directly and notify the pool explicitly.
      for (auto &task : tasks) {
        queue->push(task);
      }
      pool.wake_adapter();
    }
    wait_until(
        [&] {
          return started.load(std::memory_order_acquire) == 4 &&
                 pool.worker_count() == 4;
        },
        "submission did not grow the pool to the worker cap");
    require(pool.worker_count() == 4, "worker cap must be respected");
    release.count_down();
    wait_until(
        [&] {
          return completed.load(std::memory_order_acquire) == tasks.size();
        },
        "burst did not finish");
    wait_until([&] { return pool.worker_count() == 1; },
               "pool did not shrink after sustained idleness");
  }
}

auto shares_captured_worker_across_resizes() -> void {
  using Queue = slp::LockingThreadSafeQueue<size_t>;
  std::atomic<size_t> started{0};
  std::atomic<size_t> completed{0};
  std::atomic<size_t> total{0};
  std::latch release{1};
  // Owning an atomic makes this mutable lambda move-only. Its state must
  // survive worker retirement and be shared with newly created workers.
  auto state = std::make_unique<std::atomic<size_t>>(0);
  auto *shared_calls = state.get();
  auto worker = [calls = std::move(state), bias = size_t{7}, &started,
                 &completed, &total, &release](size_t &item) mutable {
    calls->fetch_add(1, std::memory_order_relaxed);
    started.fetch_add(1, std::memory_order_release);
    release.wait();
    total.fetch_add(item + bias, std::memory_order_relaxed);
    completed.fetch_add(1, std::memory_order_release);
  };
  auto queue = std::make_shared<Queue>();
  slp::AdaptingThreadPool<Queue, decltype(worker)> pool{
      queue, std::move(worker), 4};
  wait_until([&] { return pool.worker_count() == 1; },
             "capturing worker pool did not shrink");
  std::array<size_t, 12> tasks;
  tasks.fill(3);
  pool.submit_batch(tasks);
  wait_until(
      [&] {
        return started.load(std::memory_order_acquire) == 4 &&
               pool.worker_count() == 4;
      },
      "capturing worker pool did not regrow");
  release.count_down();
  wait_until([&] { return completed.load(std::memory_order_acquire) == 12; },
             "capturing workers did not finish");
  require(total.load(std::memory_order_relaxed) == 120,
          "workers must use the supplied captures");
  require(shared_calls->load(std::memory_order_relaxed) == 12,
          "all workers must invoke the pool's stored callable");
  wait_until([&] { return pool.worker_count() == 1; },
             "capturing worker pool did not shrink after work");
  pool.submit(size_t{3});
  wait_until([&] { return completed.load(std::memory_order_acquire) == 13; },
             "capturing worker did not process a later task");
  require(shared_calls->load(std::memory_order_relaxed) == 13,
          "captured state must survive resizing");
}

auto concurrent_submissions() -> void {
  constexpr size_t task_count = 20000;
  std::array<std::atomic<unsigned>, task_count> visits{};
  std::atomic<size_t> completed{0};
  using Queue = slp::LockingThreadSafeQueue<CountedTask>;
  using Worker = decltype([](CountedTask &task) {
    task.visits->fetch_add(1, std::memory_order_relaxed);
    task.completed->fetch_add(1, std::memory_order_release);
  });
  auto queue = std::make_shared<Queue>();
  {
    slp::AdaptingThreadPool<Queue, Worker> pool{queue, 4};
    std::vector<std::jthread> producers;
    for (size_t producer = 0; producer < 4; ++producer) {
      producers.emplace_back([&, producer] {
        for (size_t i = producer; i < task_count; i += 4) {
          pool.submit(CountedTask{&visits[i], &completed});
        }
      });
    }
    for (auto &producer : producers) {
      producer.join();
    }
    wait_until(
        [&] { return completed.load(std::memory_order_acquire) == task_count; },
        "concurrent submissions did not finish");
  }
  for (auto &count : visits) {
    require(count.load(std::memory_order_relaxed) == 1,
            "concurrent submissions must execute exactly once");
  }
}

auto rejects_null_queue() -> void {
  using Queue = slp::LockingThreadSafeQueue<size_t>;
  using Worker = decltype([](size_t &) {});
  try {
    slp::AdaptingThreadPool<Queue, Worker> pool{nullptr, 1};
    require(false, "null queue must be rejected");
  } catch (const std::invalid_argument &) {
  }
}
} // namespace

int main() {
  empty_queue_shutdown();
  processes_tasks_once(4);
  // hardware_concurrency() is allowed to report zero; keep one worker usable.
  processes_tasks_once(0);
  shutdown_joins_active_task();
  shrinks_and_regrows();
  shares_captured_worker_across_resizes();
  concurrent_submissions();
  rejects_null_queue();
  std::println("Thread pool regression checks passed");
}
