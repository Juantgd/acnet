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
      : ref(ref_count), type(type), sender_id(sender_id) {}
  ~EventMessage() = default;

  alignas(kCacheLineSize) std::atomic<std::size_t> ref{0};
  EventType type{0};
  std::size_t sender_id{0};
  // TODO: 使用类型擦除
  std::unique_ptr<PayloadBase> payload{nullptr};

  // 设置事件消息载荷数据
  template <typename T> void set(T &&value) {
    payload = std::make_unique<Payload<T>>(static_cast<T &&>(value));
  }

  // 获取事件消息载荷数据
  template <typename T> T &get() const {
    return static_cast<Payload<T> *>(payload.get())->data;
  }
};

// 事件消息结构体销毁函数
// 事件消息结构体应由new分配并初始化
inline void event_message_release(EventMessage *msg) {
  if (msg->ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete msg;
  }
}

} // namespace ac

#endif
