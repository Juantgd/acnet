#include "mail_box.h"
#include "actor_scheduler.h"

namespace ac {

MailBox::MailBox(std::size_t size) : mailbox_(size) {}

MailBox::~MailBox() {
  EventMessage *msg;
  while (mailbox_.try_dequeue(&msg)) {
    event_message_release(msg);
  }
}

MailBox::MailBoxAwaiter MailBox::Receive() { return MailBoxAwaiter(*this); }

bool MailBox::try_receive(EventMessage **message) {
  return mailbox_.try_dequeue(message);
}

void MailBox::Send(EventMessage *message) {
  if (mailbox_.try_enqueue(message)) {
    // 独占式获取协程句柄
    auto handle = std::coroutine_handle<>::from_address(
        handle_.exchange(nullptr, std::memory_order_acq_rel));
    // 如果成功抢到协程句柄,则将其放入工作线程的就绪队列中准备执行
    if (handle && !handle.done()) {
      ActorScheduler::Instance().Enqueue(handle);
    }
  } else {
    discarded_nr_.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace ac
