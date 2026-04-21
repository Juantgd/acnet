// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_EVENT_MESSAGE_H_
#define AC_INCLUDE_EVENT_MESSAGE_H_

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "helper.h"

namespace ac {

enum class EventType {
  kEventNone = 0,
  kEventExited,
  kEventModuleStop,
  kEventModuleReload,
  kEventCrashReport,
};

struct PayloadBase {};

template <typename T> struct Payload : PayloadBase {
  Payload(T &&v) : data(static_cast<T &&>(v)) {}
  T data;
};

struct EventMessage {
  EventMessage() = default;
  EventMessage(uint64_t ref_count, EventType type, uint64_t sender_id)
      : ref_(ref_count), type_(type), sender_id_(sender_id) {}
  ~EventMessage() = default;

  alignas(kCacheLineSize) std::atomic<std::size_t> ref_{0};
  EventType type_{0};
  std::size_t sender_id_{0};
  // TODO: 使用类型擦除
  std::unique_ptr<PayloadBase> payload_{nullptr};

  // 设置事件消息载荷数据
  template <typename T> void set(T &&value) {
    payload_ = std::make_unique<Payload<T>>(static_cast<T &&>(value));
  }

  // 获取事件消息载荷数据
  template <typename T> T &get() const {
    return static_cast<Payload<T> *>(payload_.get())->data;
  }
};

// 事件消息结构体销毁函数
// 事件消息结构体应由new分配并初始化
inline void event_message_release(EventMessage *msg) {
  if (msg->ref_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete msg;
  }
}

} // namespace ac

#endif
