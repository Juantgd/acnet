// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_CORE_ACTOR_EVENT_BUS_H_
#define AC_CORE_ACTOR_EVENT_BUS_H_

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "event_message.h"
#include "mail_box.h"

namespace ac {

class ActorEventBus {
public:
  static thread_local std::unordered_map<EventType, std::vector<MailBoxPtr>>
      local_event_bus;
  static thread_local std::size_t local_version;

  ~ActorEventBus() = default;

  inline static ActorEventBus &Instance() {
    static ActorEventBus event_bus;
    return event_bus;
  }

  void Subscribe(EventType type, MailBoxPtr mailbox);

  void Unsubscribe(EventType type, MailBoxPtr mailbox);

  void Publish(EventMessage *message);

private:
  ActorEventBus() = default;

  void __sync_route_table();

  // write lock
  std::mutex mutex_;
  // 事件总线,向订阅了该事件类型的Actor模块发送该事件消息
  std::unordered_map<EventType, std::vector<MailBoxPtr>> event_bus_;
  std::atomic<std::size_t> version_{0};
};

} // namespace ac

#endif
