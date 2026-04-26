// Copyright (c) 2026 juantgd. All Rights Reserved.

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "actor_scheduler.h"
#include "event_message.h"
#include "helper.h"
#include "mail_box.h"
#include "task.h"

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

ac::EventMessage *make_message(std::size_t id) {
  return new ac::EventMessage(1, ac::EventType::kEventNone, id);
}

void test_mail_queue_stress() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kMessagesPerProducer = 50000;
  constexpr std::size_t kTotalMessages =
      kProducerCount * kMessagesPerProducer;
  constexpr std::size_t kQueueSize = 1024;

  ac::MailQueue queue(kQueueSize);
  std::vector<std::uint8_t> seen(kTotalMessages, 0);
  std::latch start_latch(static_cast<std::ptrdiff_t>(kProducerCount + 1));
  std::atomic<std::size_t> consumed{0};

  std::thread consumer([&]() {
    start_latch.count_down();
    start_latch.wait();
    while (consumed.load(std::memory_order_relaxed) < kTotalMessages) {
      ac::EventMessage *message = nullptr;
      if (!queue.try_dequeue(&message)) {
        std::this_thread::yield();
        continue;
      }
      expect(message != nullptr, "MailQueue returned a null message");
      expect(message->sender_id_ < kTotalMessages, "MailQueue returned bad id");
      expect(seen[message->sender_id_] == 0, "MailQueue duplicated a message");
      seen[message->sender_id_] = 1;
      consumed.fetch_add(1, std::memory_order_relaxed);
      ac::event_message_release(&message);
    }
  });

  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);
  for (std::size_t producer_id = 0; producer_id != kProducerCount;
       ++producer_id) {
    producers.emplace_back([&, producer_id]() {
      start_latch.count_down();
      start_latch.wait();
      for (std::size_t index = 0; index != kMessagesPerProducer; ++index) {
        ac::EventMessage *message =
            make_message(producer_id * kMessagesPerProducer + index);
        while (!queue.try_enqueue(message)) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto &producer : producers) {
    producer.join();
  }
  consumer.join();

  expect(consumed.load(std::memory_order_relaxed) == kTotalMessages,
         "MailQueue did not consume every message");
  for (std::size_t i = 0; i != kTotalMessages; ++i) {
    expect(seen[i] == 1, "MailQueue lost a message");
  }
}

struct MailBoxStressState {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::uint8_t> seen;
  std::size_t total_messages{0};
  std::atomic<std::size_t> consumed{0};
  std::atomic<std::size_t> phase{0};
  std::string error;
  bool done{false};
};

ac::LaunchTask mailbox_consumer(ac::MailBoxPtr mailbox,
                                MailBoxStressState *state,
                                std::size_t producers_per_round) {
  mailbox->MarkRunning();
  while (state->consumed.load(std::memory_order_relaxed) <
         state->total_messages) {
    ac::EventMessage *message = nullptr;
    while (mailbox->try_receive(&message)) {
      if (message == nullptr) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error = "MailBox returned a null message";
        state->done = true;
        state->cv.notify_all();
        goto coro_exit;
      }
      if (message->sender_id_ >= state->total_messages) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error = "MailBox returned bad id";
        state->done = true;
        state->cv.notify_all();
        ac::event_message_release(&message);
        goto coro_exit;
      }
      if (state->seen[message->sender_id_] != 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error = "MailBox duplicated a message";
        state->done = true;
        state->cv.notify_all();
        ac::event_message_release(&message);
        goto coro_exit;
      }

      state->seen[message->sender_id_] = 1;
      std::size_t consumed =
          state->consumed.fetch_add(1, std::memory_order_relaxed) + 1;
      ac::event_message_release(&message);
      message = nullptr;

      if (consumed % producers_per_round == 0) {
        state->phase.fetch_add(1, std::memory_order_release);
      }
    }
    if (state->consumed.load(std::memory_order_relaxed) >=
        state->total_messages) {
      break;
    }
    co_await mailbox->Wait();
  }

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->done = true;
  }
  state->cv.notify_all();
coro_exit:
  co_await std::suspend_never();
}

void test_mailbox_receive_stress() {
  constexpr std::size_t kProducerCount = 4;
  constexpr std::size_t kRounds = 50000;
  constexpr std::size_t kTotalMessages = kProducerCount * kRounds;
  constexpr std::size_t kMailBoxSize = 16;

  auto mailbox = std::make_shared<ac::MailBox>(kMailBoxSize);
  MailBoxStressState state;
  state.seen = std::vector<std::uint8_t>(kTotalMessages, 0);
  state.total_messages = kTotalMessages;

  auto task = mailbox_consumer(mailbox, &state, kProducerCount);
  mailbox->ArmConsumer(task.handle_);
  ac::ActorScheduler::Instance().Enqueue(task.handle_);

  std::barrier send_barrier(static_cast<std::ptrdiff_t>(kProducerCount));
  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);

  for (std::size_t producer_id = 0; producer_id != kProducerCount;
       ++producer_id) {
    producers.emplace_back([&, producer_id]() {
      for (std::size_t round = 0; round != kRounds; ++round) {
        while (state.phase.load(std::memory_order_acquire) != round) {
          std::this_thread::yield();
        }
        send_barrier.arrive_and_wait();
        mailbox->Send(make_message(round * kProducerCount + producer_id));
      }
    });
  }

  {
    std::unique_lock<std::mutex> lock(state.mutex);
    bool completed = state.cv.wait_for(lock, 20s, [&]() { return state.done; });
    expect(completed, "MailBox stress test timed out");
    expect(state.error.empty(), state.error);
  }

  for (auto &producer : producers) {
    producer.join();
  }

  expect(state.consumed.load(std::memory_order_relaxed) == kTotalMessages,
         "MailBox did not consume every message");
  expect(mailbox->get_dropped_count() == 0, "MailBox dropped messages");
  for (std::size_t i = 0; i != kTotalMessages; ++i) {
    expect(state.seen[i] == 1, "MailBox lost a message");
  }
}

} // namespace

int main() {
  ac::Logger::Instance()->set_log_level(quill::LogLevel::None);
  Watchdog watchdog(30s);

  test_mail_queue_stress();
  test_mailbox_receive_stress();

  std::cout << "mail stress tests passed\n";
  return 0;
}
