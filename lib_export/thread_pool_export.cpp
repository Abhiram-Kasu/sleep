#include "basic_thread_safe_queue.hpp"
#include "thread_pool.hpp"
#include <any>
#include <cstdint>

extern "C" {

struct ExternValueType {
  uint64_t holder;
};

struct ExternRefType {
  void *ptr;
};
template struct slp::LockingThreadSafeQueue<ExternValueType>;
using ExternValueQueue = slp::LockingThreadSafeQueue<ExternValueType>;

template struct slp::LockingThreadSafeQueue<ExternRefType>;
using ExternRefQueue = slp::LockingThreadSafeQueue<ExternRefType>;

using RefWorkerFunctionPointer = void (*)(ExternRefType);
auto create_thread_pool_ref(RefWorkerFunctionPointer function_ptr) -> void * {

  auto worker_function = [function_ptr](ExternRefType &ref_type) {
    function_ptr(ref_type);
  };

  auto *thread_pool =
      new slp::AdaptingThreadPool<ExternRefQueue, decltype(worker_function)>{
          std::make_shared<ExternRefQueue>(), std::move(worker_function)};

  return thread_pool;
}

auto push_value_thread_pool_ref(ExternRefType ref_type, void *queue) -> void {
  // surely this is fine since the lambda takes up the same space...
  auto *q = static_cast<slp::AdaptingThreadPool<
      ExternRefQueue, decltype([padding = intptr_t{}](auto) {})> *>(queue);
  q->submit(ref_type);
}

auto clean_thread_pool(void *thread_pool) -> void {
  delete static_cast<ExternValueType *>(thread_pool);
}
}
