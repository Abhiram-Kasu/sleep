#include "runtime.hpp"
#include "task.hpp"
#include <chrono>
#include <print>
#include <thread>
using namespace std::chrono_literals;

auto test() -> slp::IOTask<void> {
  std::println("Starting await");
  co_await slp::wait_async(1000ms);
  std::println("Finished await");
};

int main() {
  test();
  std::this_thread::sleep_for(1500ms);
}
