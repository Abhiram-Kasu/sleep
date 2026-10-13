#include "runtime.hpp"
#include "task.hpp"
#include "thread_pool.hpp"
#include <chrono>
#include <print>
#include <thread>
#include "util.h"
using namespace std::chrono_literals;

int main() {


  auto queue = std::make_shared<slp::LockingThreadSafeQueue<size_t>>();


  for(auto i{0uz}; i < 1'000'000; ++i){
    queue->push(auto(i));
  }

  std::println("Pushed All Items");

  using worker_type = decltype([](size_t& number){
    std::println("{}", number);
  });
  auto tp = slp::AdaptingThreadPool<slp::LockingThreadSafeQueue<size_t>, worker_type>{queue, 5};

  while(not queue->is_empty()) {
    std::this_thread::yield();
  }



}
