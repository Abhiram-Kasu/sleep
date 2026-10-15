#include "thread_pool_export.h"
#include "basic_thread_safe_queue.hpp"
#include "thread_pool.hpp"

namespace {
using ExternRefQueue = slp::LockingThreadSafeQueue<ExternRefType>;

struct RefWorker {
  RefWorkerFunctionPointer callback;

  auto operator()(ExternRefType &ref_type) const -> void { callback(ref_type); }
};

// Creation, submission, and destruction must use the exact same pool type.
using RefThreadPool = slp::AdaptingThreadPool<ExternRefQueue, RefWorker>;
} // namespace

extern "C" {
auto create_thread_pool_ref(RefWorkerFunctionPointer function_ptr) -> void * {
  if (!function_ptr) {
    return nullptr;
  }
  try {
    return new RefThreadPool{std::make_shared<ExternRefQueue>(),
                             RefWorker{function_ptr}};
  } catch (...) {
    return nullptr;
  }
}

auto push_value_thread_pool_ref(ExternRefType ref_type, void *thread_pool)
    -> void {
  static_cast<RefThreadPool *>(thread_pool)->submit(ref_type);
}

auto clean_thread_pool(void *thread_pool) -> void {
  delete static_cast<RefThreadPool *>(thread_pool);
}
}
