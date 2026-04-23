// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_TASK_H_
#define AC_INCLUDE_TASK_H_

#include <coroutine>
#include <exception>
#include <variant>

namespace ac {

// 协程需要两种promise_type,一个通用的Task协程,可被co_await且在执行完毕后返回上一个调用者协程中
// initial_suspend返回std::suspend_never,final_suspend返回自定义Awaiter对象,以在协程执行完毕
// 后挂起协程时返回到上一个父协程中
// 一个仅用来触发的协程,initial_suspend返回std::suspend_always,以便交给事件调度器启动该协程的执行
// final_suspend返回std::suspend_never,以便让该协程执行完毕后自动销毁协程帧,且该协程不能被co_await
// 只用来作为Actor的入口
template <typename T> class Task;

template <typename T> struct TaskPromise {
  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<TaskPromise> h) noexcept {
      auto &p = h.promise();
      return p.continuation_ ? p.continuation_ : std::noop_coroutine();
    }
    void await_resume() noexcept {}
  };
  Task<T> get_return_object() noexcept;
  // 不挂起,立即执行
  std::suspend_never initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return FinalAwaiter{}; }
  void return_value(T value) noexcept { result_ = std::move(value); }
  void unhandled_exception() noexcept { result_ = std::current_exception(); }
  // 保存用于对称转移的协程句柄
  std::coroutine_handle<> continuation_;
  std::variant<std::monostate, T, std::exception_ptr> result_;
};

template <> struct TaskPromise<void> {
  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<TaskPromise> h) noexcept {
      auto &p = h.promise();
      return p.continuation_ ? p.continuation_ : std::noop_coroutine();
    }
    void await_resume() noexcept {}
  };
  Task<void> get_return_object() noexcept;
  std::suspend_never initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return FinalAwaiter{}; }
  void return_void() noexcept {}
  void unhandled_exception() noexcept { result_ = std::current_exception(); }
  // 保存用于对称转移的协程句柄
  std::coroutine_handle<> continuation_;
  std::variant<std::monostate, std::exception_ptr> result_;
};

template <typename T = void> class Task {
public:
  using promise_type = TaskPromise<T>;
  using handle_type = std::coroutine_handle<promise_type>;
  explicit Task(handle_type handle) noexcept : handle_(handle) {}
  ~Task() noexcept {
    if (handle_)
      handle_.destroy();
  }

  bool await_ready() noexcept { return !handle_ || handle_.done(); }
  void await_suspend(std::coroutine_handle<> caller) noexcept {
    handle_.promise().continuation_ = caller;
  }
  T await_resume() {
    auto &p = handle_.promise();
    if (std::holds_alternative<std::exception_ptr>(p.result_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(p.result_));
    }
    if constexpr (!std::is_void_v<T>) {
      return std::move(std::get<T>(p.result_));
    }
  }

private:
  handle_type handle_;
};

struct LaunchTask {
  struct promise_type {
    LaunchTask get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept { std::terminate(); }
  };
  explicit LaunchTask(std::coroutine_handle<> handle) : handle_(handle) {}
  std::coroutine_handle<> handle_;
};

inline LaunchTask LaunchTask::promise_type::get_return_object() noexcept {
  return LaunchTask{std::coroutine_handle<promise_type>::from_promise(*this)};
}

template <typename T> Task<T> TaskPromise<T>::get_return_object() noexcept {
  return Task{std::coroutine_handle<TaskPromise>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
  return Task{std::coroutine_handle<TaskPromise>::from_promise(*this)};
}

} // namespace ac

#endif
