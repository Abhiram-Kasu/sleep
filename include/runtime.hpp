#pragma once
#include "basic_thread_safe_queue.hpp"

namespace slp {
// We need a queue to add tasks to and threads to pull these tasks

class Scheduler {
  struct IOTask {};

  using Queue = LockingThreadSafeQueue<IOTask>;

public:
private:
};

} // namespace slp
