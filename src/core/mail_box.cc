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

MailBox::WaitAwaiter MailBox::Wait() { return WaitAwaiter(*this); }

void MailBox::ArmConsumer(std::coroutine_handle<> handle) {
  consumer_.store(handle.address(), std::memory_order_relaxed);
  scheduled_.store(true, std::memory_order_release);
}

void MailBox::MarkRunning() {
  scheduled_.store(true, std::memory_order_release);
}

bool MailBox::try_receive(EventMessage **message) {
  bool result = mailbox_.try_dequeue(message);
  if (result) {
    solved_nr_.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

void MailBox::Send(EventMessage *message) {
  if (mailbox_.try_enqueue(message)) {
    if (!scheduled_.exchange(true, std::memory_order_acq_rel)) {
      auto handle = std::coroutine_handle<>::from_address(
          consumer_.load(std::memory_order_acquire));
      if (handle && !handle.done()) {
        ActorScheduler::Instance().Enqueue(handle);
      }
    }
  } else {
    // drop and decrease reference count
    event_message_release(&message);
    discarded_nr_.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace ac
