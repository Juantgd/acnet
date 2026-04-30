// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "event_bus.h"

namespace ac {

thread_local std::unordered_map<EventType, std::vector<MailBoxPtr>>
    ActorEventBus::local_event_bus;

thread_local std::size_t ActorEventBus::local_version = 0;

void ActorEventBus::Subscribe(EventType type, MailBoxPtr mailbox) {
  LOG_D("Subscribe event, type: {}, mailbox address: {}",
        event_type_to_string(type), (void *)mailbox.get());
  std::lock_guard<std::mutex> lock(mutex_);
  event_bus_[type].push_back(mailbox);
  version_.fetch_add(1, std::memory_order_release);
}

void ActorEventBus::Unsubscribe(EventType type, MailBoxPtr mailbox) {
  LOG_D("Unsubscribe event, type: {}, mailbox address: {}",
        event_type_to_string(type), (void *)mailbox.get());
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = event_bus_.find(type);
  if (it != event_bus_.end()) {
    for (std::size_t i = 0; i < it->second.size(); ++i) {
      if (mailbox == it->second[i]) {
        it->second[i] = std::move(it->second.back());
        it->second.pop_back();
        version_.fetch_add(1, std::memory_order_release);
        if (it->second.empty()) {
          event_bus_.erase(it);
        }
        break;
      }
    }
  }
}

void ActorEventBus::Publish(EventMessage *message) {
  if (local_version < version_.load(std::memory_order_relaxed)) [[unlikely]] {
    __sync_route_table();
  }
  auto it = local_event_bus.find(message->type_);
  if (it != local_event_bus.end()) {
    // it->second.size()总是大于等于1
    message->ref_.store(it->second.size(), std::memory_order_relaxed);
    for (auto &mb : it->second) {
      mb->Send(message);
    }
    return;
  }
  event_message_release(&message);
}

void ActorEventBus::__sync_route_table() {
  std::lock_guard<std::mutex> lock(mutex_);
  local_event_bus = event_bus_;
  local_version = version_.load(std::memory_order_relaxed);
}

} // namespace ac
