// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_CORE_MAIL_BOX_H_
#define AC_CORE_MAIL_BOX_H_

#include <memory>

#include "event_message.h"
#include "queue.h"

namespace ac {

// template class SPSCQueue<EventMessage *, kMailBoxSize>;

// MailBox是面向Actor协程的单消费者邮箱。
// Send()只负责投递消息；若当前消费者既不在运行、也不在调度队列中，
// 则负责把已绑定的消费者协程重新放回调度器，保证一次只投递一次。
// 消费者被调度后，应先通过try_receive()批量drain邮箱，再在邮箱为空时调用Wait()挂起。
//
// 使用方式:
// 1. 在消费者协程首次进入调度器前，通过ArmConsumer()绑定句柄。
// 2. 在消费者循环中:
//    while (mail_box_->try_receive(&message)) { ... }
//    co_await mail_box_->Wait();
class MailBox {
private:
  struct WaitAwaiter {
    explicit WaitAwaiter(MailBox &mail) : mail_(mail) {}
    bool await_ready() noexcept { return mail_.mailbox_.can_dequeue(); }
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
      // 先发布消费者句柄，再清除scheduled_。
      // 这样发送线程一旦观察到scheduled_ == false，就可以安全地重新调度该协程。
      mail_.consumer_.store(handle.address(), std::memory_order_relaxed);
      // Use an RMW here so the consumer synchronizes with the last sender that
      // observed it as scheduled before deciding to sleep.
      mail_.scheduled_.exchange(false, std::memory_order_acq_rel);
      // 关闭“刚确认邮箱为空、协程准备挂起时消息恰好到达”的竞争窗口。
      if (mail_.mailbox_.can_dequeue() &&
          !mail_.scheduled_.exchange(true, std::memory_order_acq_rel)) {
        return false;
      }
      return true;
    }
    void await_resume() noexcept {}

    MailBox &mail_;
  };

  // 丢弃的事件消息数量
  std::atomic<std::size_t> discarded_nr_{0};
  // 成功出队并交给消费者处理的消息数量
  std::atomic<std::size_t> solved_nr_{0};
  // 绑定到该邮箱的唯一消费者协程句柄
  std::atomic<void *> consumer_{nullptr};
  // 当前消费者是否已经在运行，或已经进入调度队列等待运行
  std::atomic<bool> scheduled_{false};
  // 多生产者单消费者消息队列
  MailQueue mailbox_;

public:
  MailBox(std::size_t size = kMailBoxSize);
  // 析构前释放邮箱中残留的消息
  ~MailBox();

  // 当邮箱为空时挂起消费者; 若消息已到达则立即继续执行
  WaitAwaiter Wait();
  // 在首次把LaunchTask放入调度器前绑定消费者协程,并标记为已调度
  void ArmConsumer(std::coroutine_handle<> handle);
  // 消费者协程恢复执行后调用,表示当前已处于running/scheduled状态
  void MarkRunning();
  // 从邮箱中弹出一条消息; 适合在一次调度中批量drain
  bool try_receive(EventMessage **message);
  // 发送一条消息; 若消费者当前未被调度,则负责把它重新放回调度器
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
