// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "mail_box.h"
#include "actor_scheduler.h"

namespace ac {

MailBox::MailBox(std::size_t size) : mailbox_(size) {}

MailBox::~MailBox() {
  EventMessage *msg;
  while (mailbox_.try_dequeue(&msg)) {
    event_message_release(&msg);
  }
}

MailBox::MailBoxAwaiter MailBox::Receive() { return MailBoxAwaiter(*this); }

bool MailBox::try_receive(EventMessage **message) {
  bool result = mailbox_.try_dequeue(message);
  if (result) {
    solved_nr_.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

void MailBox::Send(EventMessage *message) {
  if (mailbox_.try_enqueue(message)) {
    WaitState expected = WaitState::kWaiting;
    if (state_.compare_exchange_strong(expected, WaitState::kScheduled,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
      // 独占式获取协程句柄,仅允许一个发送线程把等待中的协程重新放回调度队列
      auto handle = std::coroutine_handle<>::from_address(
          waiter_.exchange(nullptr, std::memory_order_acq_rel));
      if (handle && !handle.done()) {
        scheduled_nr_.fetch_add(1, std::memory_order_relaxed);
        ActorScheduler::Instance().Enqueue(handle);
      } else {
        state_.store(WaitState::kRunning, std::memory_order_release);
      }
    } else if (expected == WaitState::kScheduled) {
      duplicate_schedule_nr_.fetch_add(1, std::memory_order_relaxed);
    }
  } else {
    // drop and decrease reference count
    event_message_release(&message);
    discarded_nr_.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace ac
