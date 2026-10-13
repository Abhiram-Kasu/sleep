#include "basic_thread_safe_queue.hpp"
#include "thread_pool.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <latch>
#include <memory>
#include <print>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
constexpr size_t max_workers = 5;
constexpr std::array<size_t, 3> burst_sizes{20, 40, 60};

struct BurstState {
  std::array<std::atomic<unsigned>, burst_sizes.back()> visits{};
  std::atomic<size_t> started{0};
  std::atomic<size_t> completed{0};
  std::latch release{1};
};

struct DemoTask {
  BurstState *state{};
  size_t index{};
};

// Always release a blocked burst when a check fails, so pool shutdown can join
// its callbacks. The burst states themselves outlive the pool.
struct ReleaseGate {
  std::latch &gate;
  bool released{false};

  auto release() noexcept -> void {
    if (!released) {
      gate.count_down();
      released = true;
    }
  }
  ~ReleaseGate() { release(); }
};

auto require(bool condition, const char *message) -> void {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

template <typename Predicate>
auto wait_for(Predicate predicate, const char *failure) -> void {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, failure);
    std::this_thread::sleep_for(1ms);
  }
}

auto run_resize_demo() -> void {
  // Declare state before the pool so even failure cleanup keeps task data alive.
  std::array<BurstState, burst_sizes.size()> states{};
  using Queue = slp::LockingThreadSafeQueue<DemoTask>;
  using Worker = decltype([](DemoTask &task) {
    task.state->started.fetch_add(1, std::memory_order_release);
    task.state->release.wait();
    std::this_thread::sleep_for(15ms);
    task.state->visits[task.index].fetch_add(1, std::memory_order_relaxed);
    task.state->completed.fetch_add(1, std::memory_order_release);
  });
  using Pool = slp::AdaptingThreadPool<Queue, Worker, 0.2f, 1.5f, true>;
  auto queue = std::make_shared<Queue>();
  Pool pool{queue, max_workers};

  std::println("[demo] waiting for the initial idle pool to shrink to one worker");
  wait_for([&] { return pool.worker_count() == 1; },
           "initial idle pool did not shrink to one worker");

  size_t total_completed = 0;
  for (size_t round = 0; round < burst_sizes.size(); ++round) {
    auto &state = states[round];
    const auto task_count = burst_sizes[round];
    std::vector<DemoTask> tasks;
    tasks.reserve(task_count);
    for (size_t i = 0; i < task_count; ++i) {
      tasks.push_back(DemoTask{&state, i});
    }

    ReleaseGate gate{state.release};
    std::println("\n[demo] burst {}: submitting {} tasks from one worker",
                 round + 1, task_count);
    pool.submit_batch(tasks);

    // Keep callbacks blocked until growth is observed. Quick callbacks cannot
    // drain the queue before the adapter is scheduled and hide a resize failure.
    wait_for(
        [&] {
          require(pool.worker_count() <= max_workers,
                  "pool exceeded its configured worker cap");
          return pool.worker_count() == max_workers &&
                 state.started.load(std::memory_order_acquire) == max_workers;
        },
        "pool did not grow to five active workers");
    require(state.completed.load(std::memory_order_acquire) == 0,
            "a task bypassed the burst release gate");
    std::println("[demo] verified {} active workers; releasing burst {}",
                 max_workers, round + 1);
    gate.release();

    wait_for(
        [&] {
          require(pool.worker_count() <= max_workers,
                  "pool exceeded its configured worker cap");
          return state.completed.load(std::memory_order_acquire) == task_count;
        },
        "burst tasks did not finish");
    require(queue->is_empty(), "completed burst left queued tasks");
    for (size_t i = 0; i < task_count; ++i) {
      require(state.visits[i].load(std::memory_order_acquire) == 1,
              "a burst task was lost or executed more than once");
    }
    total_completed += task_count;
    std::println("[demo] verified all {} tasks ran exactly once; waiting for idle shrink",
                 task_count);

    // No extra signals: the adapter's timeout must detect sustained idleness.
    wait_for([&] { return pool.worker_count() == 1; },
             "pool did not shrink back to one worker after the burst");
    std::println("[demo] burst {} passed: workers 1 -> {} -> 1", round + 1,
                 max_workers);
  }

  std::println("\nResize demonstration passed: {} bursts, {} tasks exactly once",
               burst_sizes.size(), total_completed);
}
} // namespace

int main() {
  try {
    run_resize_demo();
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::println(stderr, "Resize demonstration failed: {}", error.what());
    return EXIT_FAILURE;
  }
}
