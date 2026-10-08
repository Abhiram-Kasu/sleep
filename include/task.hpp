#include <chrono>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <span>
#include <thread>
#include <type_traits>

namespace slp {
//
// struct SocketWriteTask {
//   struct promise_type {
//     // Called on coroutine frame allocation
//     auto get_return_object() -> SocketWriteTask {
//       return SocketWriteTask{
//           std::coroutine_handle<promise_type>::from_promise(*this)};
//     }
//     auto return_value(size_t num_bytes_written) {
//       m_num_bytes_written = num_bytes_written;
//     }
//
//     auto initial_suspend() -> std::suspend_never { return {}; }
//     // Allow for bytes to be read from the Task
//     auto final_suspend() -> std::suspend_always { return {}; }
//
//   private:
//     size_t m_num_bytes_written;
//   };
//   std::coroutine_handle<promise_type> handle;
// };
//
// template <typename T> struct IOTask {
//   struct promise_type {
//     static constexpr auto has_value_to_return = std::is_void_v<T>;
//
//     IOTask get_return_object() {
//       return
//       IOTask{std::coroutine_handle<promise_type>::from_promise(*this)};
//     }
//     std::suspend_never initial_suspend() noexcept { return {}; }
//     std::suspend_never final_suspend() noexcept { return {}; }
//
//     T return_value()
//       requires has_value_to_return
//     {}
//     void return_void()
//       requires(!has_value_to_return)
//     {}
//     void unhandled_exception() { std::terminate(); }
//   };
//
//   std::coroutine_handle<promise_type> handle;
// };
//
// template <typename Duration, typename Period> struct TimedWait {
//   std::chrono::duration<Duration, Period> duration;
//   auto await_ready() const noexcept -> bool { return false; }
//   auto await_suspend(std::coroutine_handle<> handle) const noexcept -> void {
//
//     const auto d = duration;
//     std::thread thread{[d, handle]() {
//       std::this_thread::sleep_for(d);
//       handle.resume();
//     }};
//     thread.detach();
//   }
//
//   auto await_resume() const noexcept -> void { return; }
// };
// template <typename Duration, typename Period>
// auto wait_async(std::chrono::duration<Duration, Period> duration)
//     -> TimedWait<Duration, Period> {
//   return TimedWait{duration};
// }

} // namespace slp
