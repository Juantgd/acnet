// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_module.h"

#include <chrono>
#include <exception>

using namespace std::chrono_literals;

namespace ac {

ActorModule::ActorModule(std::size_t id, MailBoxPtr parent_mailbox)
    : actor_id_(id), parent_mailbox_(parent_mailbox) {}

Task<ActorExitState> ActorModule::RunCoroutine(MailBoxPtr &mailbox) {
  LOG_I("[NetActor id: {}] module start working...", actor_id_);
  mailbox->MarkRunning();
  EventMessage *msg = nullptr;
  try {
    std::this_thread::sleep_for(3s);
    throw std::runtime_error("boom!boom!boom!");
    while (true) {
      while (mailbox->try_receive(&msg)) {
        if (msg->type_ == EventType::kEventCmdModuleStop) {
          event_message_release(&msg);
          LOG_I("[NetActor id: {}] module shutdown...", actor_id_);
          co_return ActorExitState::kExitSuccess;
        }
        ProcessEvent(msg);
        event_message_release(&msg);
      }
      co_await mailbox->Wait();
    }
  } catch (const std::exception &e) {
    // customize error handle
    this->error_handle(e);
    // 记录协程崩溃时所处理的事件消息类型
    EventType event_type = msg ? msg->type_ : EventType::kEventNone;
    event_message_release(&msg);
    EventMessage *crash_msg =
        new EventMessage(1, EventType::kEventCrashReport, actor_id_);
    crash_msg->set(std::format("[NetActor id: {}] event_type: {}, error: {}",
                               actor_id_, event_type_to_string(event_type),
                               e.what()));
    parent_mailbox_->Send(crash_msg);
    co_return ActorExitState::kExitFailure;
  }
}

} // namespace ac
