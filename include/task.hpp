#include <chrono>
#include <coroutine>
#include <exception>
#include <thread>

namespace slp {
template <typename T> struct IOTask {
  struct promise_type {
    IOTask get_return_object() {
      return IOTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };

  std::coroutine_handle<promise_type> handle;
};

template <typename Duration, typename Period> struct TimedWait {
  std::chrono::duration<Duration, Period> duration;
  auto await_ready() const noexcept -> bool { return false; }
  auto await_suspend(std::coroutine_handle<> handle) const noexcept -> void {

    const auto d = duration;
    std::thread thread{[d, handle]() {
      std::this_thread::sleep_for(d);
      handle.resume();
    }};
    thread.detach();
  }

  auto await_resume() const noexcept -> void { return; }
};
template <typename Duration, typename Period>
auto wait_async(std::chrono::duration<Duration, Period> duration)
    -> TimedWait<Duration, Period> {
  return TimedWait{duration};
}

} // namespace slp
