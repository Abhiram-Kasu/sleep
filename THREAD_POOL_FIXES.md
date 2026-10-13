# Thread pool build, shutdown, and adapter implementation

Running `ninja` in `build/` originally failed with four compiler errors in
`include/thread_pool.hpp`. The fixes address those errors and the worker lifetime
problems identified during the review. Those initial changes were committed as
`7cd54ed` (`Fix thread pool template construction and worker shutdown`). The
follow-up implementation activates the adapter and adds cooperative wakeups.

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

The worker-removal code also joins a stopped worker before erasing its
list node. `ThreadWrapper` declares its atomic state before its thread handle, so
reverse member destruction destroys the thread handle first.

If construction fails after some workers have started, the constructor uses the
same shutdown helper before rethrowing the exception. This prevents those workers
from accessing a partially destroyed pool. A requested worker count of zero is
normalized to one, including when `hardware_concurrency()` returns zero.

The header explicitly includes the facilities it uses.

## Verification

CTest runs `tests/thread_pool_test.cpp`, with a 15-second timeout to catch shutdown
hangs. It covers:

- Creating and destroying 100 pools with empty queues.
- Executing 10,000 tasks exactly once with four workers, including work added
  after pool startup.
- Executing another 10,000 tasks with a requested worker count of zero.
- Destroying a pool during an active callback and checking that the callback has
  completed before destruction returns.

Initial fix results:

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

The constructor's exception cleanup and thread-creation failure recovery were
reviewed but are not directly exercised by fault-injection tests.

## Cooperative adapter wakeups

The adapter runs in a separate `std::jthread`. Workers signal `wake_adapter()`
when they enter or leave an actual empty-queue wait, and after completing a task.
They do not signal on each iteration of the polling loop.

Producers use `submit(item)` to enqueue one task and notify the adapter, or
`submit_batch(range)` to enqueue a range and notify once at the end. A failed
batch also notifies before rethrowing, because some earlier tasks may already
have been enqueued. Code that pushes directly into the shared queue can call
`wake_adapter()` explicitly. The example now submits work through the pool.

The wakeup mechanism uses an event counter protected by a mutex, together with a
condition variable. An atomic wait has no timeout, so a condition variable lets
the adapter wake both on events and on sustained idleness. Updating the counter
under the same mutex used for waiting prevents a signal from being lost between
the predicate check and entering the wait.

The adapter captures the event counter before inspecting the pool, releases the
mutex while inspecting and resizing, then waits for a different counter value
or shutdown. Signals received during inspection remain visible in the counter.
Several signals can be handled together by one inspection of the current state.
The mutex is never held while creating or joining workers.

## Active resizing and ownership

The constructor starts the configured maximum worker count, as before, and then
launches the adapter. Only the adapter modifies the worker list during normal
operation. Workers publish atomic waiting state and an idle timestamp; callers
can inspect the atomic `worker_count()` snapshot without accessing the list.

Resizing follows these rules:

- Grow while queued tasks exceed `MaxQueuePressure * current_worker_count`, up
  to the configured maximum.
- Shrink only while the queue is empty and idle workers exceed the
  `MaxWaiting` fraction of the current worker count.
- A worker must have been continuously idle for at least 250 ms to be removed.
- Keep at least one worker, so later submissions can always make progress.
- Limit resize evaluations to once every 25 ms. With no new events, inspect
  every 50 ms so sustained idleness still triggers shrinking.
- If growing fails to allocate a node or create a thread, retain the existing
  workers and retry on a later evaluation.

This replaces the dormant resizing calculations, which mixed floating-point and
integer arguments to `std::min`, counted a nonexistent adapter in the worker
list, and could prune every worker. Null queues are rejected at construction.

Shutdown marks the pool as no longer alive, signals and joins the adapter, then
requests stop on and joins the workers. Joining the adapter first prevents the
destructor from accessing a list that is still being resized. Shutdown bypasses
the adapter's resize cooldown and timed wait.

## Adapter verification

The expanded regression test covers the original lifecycle cases plus:

- Shrinking to one idle worker without receiving any additional notifications.
- Three complete shrink/grow cycles, using batch submission, individual
  submission, and direct queue pushes followed by `wake_adapter()`.
- Growing to the configured cap while callbacks are held open, then completing
  every task after releasing them.
- Exactly-once execution of 20,000 tasks submitted by four concurrent producers.
- Rejecting a null queue.

Validation passed with `ninja`, CTest, AddressSanitizer/UndefinedBehaviorSanitizer,
and ThreadSanitizer. Before replacing it with the resize demonstration, the
million-item example exited successfully, and its output was checked to contain
every value from 0 through 999,999 exactly once.

Idle workers still poll with `yield()`. This implementation gives the adapter
timed sleeping and cooperative wakeups; it does not add stop-aware blocking pops
for the worker queue or exception propagation from callbacks.

## Resize demonstration and compile-time logging

`src/main.cpp` now provides a deterministic resize demonstration rather than
printing a million task values. It starts with five workers, waits for the idle
pool to shrink to one, then submits bursts of 20, 40, and 60 tasks. Each burst
holds its callbacks behind a latch until five workers are active, making growth
observable even on a fast machine. After releasing the latch, it checks that
every task ran exactly once and waits for the pool to shrink back to one without
additional signals. Failed checks and five-second timeouts produce a nonzero
exit code.

Task state outlives the pool, and a release guard opens a burst's latch even if a
check throws. This allows failure cleanup to join active workers safely.

The pool has a final `bool Verbose = false` non-type template parameter. Existing
template arguments retain their positions. To enable logging:

```cpp
using Pool = slp::AdaptingThreadPool<Queue, Worker, 0.2f, 1.5f, true>;
```

`if constexpr (Verbose)` guards startup and resize logging. Successful growth
reports the previous and resulting worker counts, queued-task snapshot, pressure
target, and cap. Successful shrinking reports the worker counts, waiting-worker
snapshot, and idle grace period. Disabled logging is discarded during template
instantiation; ordinary pools remain silent. Logging occurs on actual changes,
not on every adapter inspection.

CTest includes both the existing `thread_pool_test` and the new `resize_demo`,
each with a 15-second timeout. Use `ctest -V` in `build/` to see the demonstration's
phase messages and adapter resize logs.
