# Thread pool build and shutdown fixes

Running `ninja` in `build/` originally failed with four compiler errors in
`include/thread_pool.hpp`. The fixes address those errors and the worker lifetime
problems identified during the review.

## Why the template failed to compile

1. **Inserting a non-movable wrapper.** `m_threads.push_back({})` creates a
   temporary `ThreadWrapper` and tries to move it into the list. Its
   `std::atomic<bool>` member prevents copying and moving. Using `emplace_back()`
   constructs the wrapper directly in a stable list node.
2. **Passing an unbound member function.** `worker` needs a pool instance, so it
   cannot be passed as a plain function to `std::jthread`. The replacement lambda
   captures `this` and the list node by reference. Its `std::stop_token` parameter
   receives the token provided by `jthread`, then forwards it to `worker`.
3. **Modifying a const wrapper.** `const auto& back` prevented assignments to the
   busy flag and thread handle. The wrapper is now accessed through `auto&`.
   Its busy flag is initialized to `false` before starting the thread.
4. **Binding a reference to a temporary.** `QueueItemType& item = {}` could not
   create a writable queue destination. `QueueItemType item{}` provides a local
   object that `try_pop(item)` can populate.

## How shutdown now works

Workers remain owned by their `std::jthread` handles; they are no longer detached.
The empty-queue polling loop checks both the stop token and the pool's alive flag,
so an idle worker can exit when shutdown is requested. That exit also decrements
the waiting-worker counter.

The shared `stop_workers()` helper performs shutdown in this order:

1. Mark the pool as no longer alive.
2. Request stop on every worker.
3. Join every joinable worker while the pool, queue, and wrapper state still exist.
4. Clear the wrapper list after all workers have exited.

The existing worker-removal code also joins a stopped worker before erasing its
list node. `ThreadWrapper` declares its busy flag before its thread handle, so
reverse member destruction destroys the thread handle first.

If construction fails after some workers have started, the constructor uses the
same shutdown helper before rethrowing the exception. This prevents those workers
from accessing a partially destroyed pool. A requested worker count of zero is
normalized to one, including when `hardware_concurrency()` returns zero.

The header now explicitly includes `<algorithm>`, `<concepts>`, and `<memory>` for
the facilities it uses.

## Verification

CTest runs `tests/thread_pool_test.cpp`, with a 15-second timeout to catch shutdown
hangs. It covers:

- Creating and destroying 100 pools with empty queues.
- Executing 10,000 tasks exactly once with four workers, including work added
  after pool startup.
- Executing another 10,000 tasks with a requested worker count of zero.
- Destroying a pool during an active callback and checking that the callback has
  completed before destruction returns.

Results:

- `ninja` in `build/`: passed.
- `ctest --output-on-failure` in `build/`: passed, 1/1 test.
- Existing million-item `sleep` program: exited successfully, approximately
  1.56 seconds with standard output redirected to `/dev/null`.
- The regression executable built with AddressSanitizer and
  UndefinedBehaviorSanitizer: passed without diagnostics.
- `git diff --check`: passed.

To rerun the build and regression checks:

```sh
cd build
ninja
ctest --output-on-failure
```

## Behavior limits

Shutdown waits for callbacks already executing; it does not guarantee that all
tasks still queued will run. A callback that never returns will prevent its worker
from joining. Idle workers still use the existing yield-based polling strategy.

The existing `manage_threads()` routine is not launched by the constructor, so
adaptive resizing remains inactive. These changes fix the reported build and
worker shutdown problems; they do not enable the unfinished resizing mechanism.
The constructor's exception cleanup and the inactive manager's removal path were
reviewed but are not directly exercised by the regression test.
