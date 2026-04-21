// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_CORE_MAIL_BOX_H_
#define AC_CORE_MAIL_BOX_H_

#include <memory>

#include "event_message.h"
#include "queue.h"

namespace ac {

// template class SPSCQueue<EventMessage *, kMailBoxSize>;

// Usage: EventMessage* message = co_await mail_box_->Receive();
// 当message ==
// nullptr时,被动唤醒,需要使用mail_box_->try_dequeue()获取最新事件消息
class MailBox {
private:
  struct MailBoxAwaiter {
    MailBoxAwaiter(MailBox &mail) : mail_(mail), msg_(nullptr) {}
    bool await_ready() noexcept { return mail_.mailbox_.try_dequeue(&msg_); }
    // coawait操作可能运行在别的线程上与Send操作所在的线程不一致,
    // 且可能同时有多个线程向该收件箱使用Send操作,但同一时刻只有一个线程在进行coawait操作,
    // 所以需要使用原子存储操作确保当前调度器工作线程对该协程的挂起操作对其他工作线程可见
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
      if (mail_.mailbox_.try_dequeue(&msg_)) {
        return false;
      }
      mail_.handle_.store(handle.address(), std::memory_order_release);
      return true;
    }
    EventMessage *await_resume() noexcept { return msg_; }

    MailBox &mail_;
    EventMessage *msg_;
  };

  // 丢弃的事件消息数量
  std::atomic<std::size_t> discarded_nr_{0};
  std::atomic<std::size_t> solved_nr_{0};
  // 因等待收件箱而挂起的协程句柄
  std::atomic<void *> handle_{nullptr};
  // 收件箱
  MailQueue mailbox_;

public:
  MailBox(std::size_t size = kMailBoxSize);
  // release message before free
  ~MailBox();

  // 当收件箱中没有事件消息时,挂起协程
  MailBoxAwaiter Receive();
  // 手动获取消息
  bool try_receive(EventMessage **message);
  // 当收件箱接收到事件消息时,恢复挂起的协程
  void Send(EventMessage *message);

  inline std::size_t get_dropped_count() const {
    return discarded_nr_.load(std::memory_order_relaxed);
  }
  inline std::size_t get_solved_count() const {
    return solved_nr_.load(std::memory_order_relaxed);
  }
};

typedef std::shared_ptr<MailBox> MailBoxPtr;

} // namespace ac

#endif
