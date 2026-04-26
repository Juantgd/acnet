// Copyright (c) 2026 juantgd. All Rights Reserved.

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "queue.h"

namespace {

using namespace std::chrono_literals;

class Watchdog {
public:
  explicit Watchdog(std::chrono::seconds timeout)
      : thread_([timeout, this]() {
          std::unique_lock<std::mutex> lock(mutex_);
          if (cv_.wait_for(lock, timeout,
                           [&]() { return !armed_.load(std::memory_order_acquire); })) {
            return;
          }
          std::cerr << "test timeout after " << timeout.count() << " seconds\n";
          std::_Exit(EXIT_FAILURE);
        }) {}

  ~Watchdog() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      armed_.store(false, std::memory_order_release);
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::atomic<bool> armed_{true};
  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
};

[[noreturn]] void fail(std::string message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string &message) {
  if (!condition) {
    fail(message);
  }
}

void store_failure(std::atomic<bool> &failed, std::mutex &mutex, std::string &error,
                   std::string message) {
  bool expected = false;
  if (!failed.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex);
  error = std::move(message);
}

struct TestTask {
  struct promise_type {
    TestTask get_return_object() noexcept {
      return TestTask{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  explicit TestTask(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  TestTask(TestTask &&other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}

  TestTask &operator=(TestTask &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  TestTask(const TestTask &) = delete;
  TestTask &operator=(const TestTask &) = delete;

  ~TestTask() {
    if (handle_) {
      handle_.destroy();
    }
  }

  std::coroutine_handle<> handle() const noexcept { return handle_; }

private:
  std::coroutine_handle<promise_type> handle_;
};

TestTask make_test_task() { co_return; }

void test_work_stealing_queue_empty_race() {
  constexpr std::size_t kThiefCount = 4;
  constexpr std::size_t kIterations = 2'000'000;

  ac::WorkStealingQueue<32> queue;
  std::barrier start_latch(static_cast<std::ptrdiff_t>(kThiefCount + 1));
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error;

  std::thread owner([&]() {
    start_latch.arrive_and_wait();
    for (std::size_t i = 0; i != kIterations; ++i) {
      if (failed.load(std::memory_order_relaxed)) {
        return;
      }
      std::coroutine_handle<> handle = nullptr;
      auto result = queue.pop(handle);
      if (result != ac::StealState::kEmpty || handle) {
        store_failure(failed, error_mutex, error,
                      "empty pop returned a task while racing with steal");
        return;
      }
    }
  });

  std::vector<std::thread> thieves;
  thieves.reserve(kThiefCount);
  for (std::size_t thief_id = 0; thief_id != kThiefCount; ++thief_id) {
    thieves.emplace_back([&]() {
      start_latch.arrive_and_wait();
      for (std::size_t i = 0; i != kIterations; ++i) {
        if (failed.load(std::memory_order_relaxed)) {
          return;
        }
        std::coroutine_handle<> handle = nullptr;
        auto result = queue.steal(handle);
        if (result != ac::StealState::kEmpty || handle) {
          store_failure(failed, error_mutex, error,
                        "empty steal observed a phantom task during owner pop");
          return;
        }
      }
    });
  }

  owner.join();
  for (auto &thief : thieves) {
    thief.join();
  }

  expect(!failed.load(std::memory_order_relaxed), error);
}

void test_work_stealing_queue_multi_thief_stress() {
  constexpr std::size_t kQueueSize = 256;
  constexpr std::size_t kTaskCount = 50'000;
  constexpr std::size_t kThiefCount = 4;

  ac::WorkStealingQueue<kQueueSize> queue;
  std::vector<TestTask> tasks;
  tasks.reserve(kTaskCount);
  std::unordered_map<void *, std::size_t> task_ids;
  task_ids.reserve(kTaskCount);

  for (std::size_t i = 0; i != kTaskCount; ++i) {
    tasks.push_back(make_test_task());
    auto handle = tasks.back().handle();
    expect(static_cast<bool>(handle), "failed to create test coroutine handle");
    task_ids.emplace(handle.address(), i);
  }

  std::vector<std::atomic<std::uint8_t>> seen(kTaskCount);
  for (auto &slot : seen) {
    slot.store(0, std::memory_order_relaxed);
  }

  std::barrier start_latch(static_cast<std::ptrdiff_t>(kThiefCount + 1));
  std::atomic<std::size_t> consumed{0};
  std::atomic<bool> failed{false};
  std::mutex error_mutex;
  std::string error;

  auto record_handle = [&](std::coroutine_handle<> handle, const char *source) {
    if (!handle) {
      store_failure(failed, error_mutex, error,
                    std::string(source) + " returned a null handle");
      return;
    }
    auto it = task_ids.find(handle.address());
    if (it == task_ids.end()) {
      store_failure(failed, error_mutex, error,
                    std::string(source) + " returned an unknown handle");
      return;
    }
    if (seen[it->second].fetch_add(1, std::memory_order_relaxed) != 0) {
      store_failure(failed, error_mutex, error,
                    std::string(source) + " duplicated a task");
      return;
    }
    consumed.fetch_add(1, std::memory_order_relaxed);
  };

  std::thread owner([&]() {
    start_latch.arrive_and_wait();
    std::size_t next_task = 0;
    while (!failed.load(std::memory_order_relaxed) &&
           consumed.load(std::memory_order_relaxed) < kTaskCount) {
      if (next_task < kTaskCount && queue.push(tasks[next_task].handle())) {
        ++next_task;
        continue;
      }

      std::coroutine_handle<> handle = nullptr;
      auto result = queue.pop(handle);
      if (result == ac::StealState::kSuccess) {
        record_handle(handle, "pop");
        continue;
      }
      if (result != ac::StealState::kEmpty) {
        store_failure(failed, error_mutex, error,
                      "owner pop returned an unexpected state");
        return;
      }
      std::this_thread::yield();
    }
  });

  std::vector<std::thread> thieves;
  thieves.reserve(kThiefCount);
  for (std::size_t thief_id = 0; thief_id != kThiefCount; ++thief_id) {
    thieves.emplace_back([&]() {
      start_latch.arrive_and_wait();
      while (!failed.load(std::memory_order_relaxed) &&
             consumed.load(std::memory_order_relaxed) < kTaskCount) {
        std::coroutine_handle<> handle = nullptr;
        auto result = queue.steal(handle);
        if (result == ac::StealState::kSuccess) {
          record_handle(handle, "steal");
          continue;
        }
        if (result != ac::StealState::kAbort &&
            result != ac::StealState::kEmpty) {
          store_failure(failed, error_mutex, error,
                        "steal returned an unexpected state");
          return;
        }
        if (result == ac::StealState::kEmpty) {
          std::this_thread::yield();
        }
      }
    });
  }

  owner.join();
  for (auto &thief : thieves) {
    thief.join();
  }

  expect(!failed.load(std::memory_order_relaxed), error);
  expect(consumed.load(std::memory_order_relaxed) == kTaskCount,
         "work stealing queue lost tasks");
  for (const auto &slot : seen) {
    expect(slot.load(std::memory_order_relaxed) == 1,
           "work stealing queue did not consume each task exactly once");
  }
}

} // namespace

int main() {
  Watchdog watchdog(30s);

  test_work_stealing_queue_empty_race();
  test_work_stealing_queue_multi_thief_stress();

  std::cout << "work stealing queue stress tests passed\n";
  return 0;
}
