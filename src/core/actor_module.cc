// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_module.h"

namespace ac {

ActorModule::ActorModule(std::size_t id, MailBoxPtr parent_mailbox)
    : actor_id_(id), parent_mailbox_(parent_mailbox) {}

Task<ActorExitState> ActorModule::RunCoroutine(MailBoxPtr &mailbox) {
  LOG_I("[NetActor id: {}] module start working...", actor_id_);
  mailbox->MarkRunning();
  EventMessage *msg = nullptr;
  try {
    while (true) {
      while (mailbox->try_receive(&msg)) {
        if (msg->type_ != EventType::kEventCmdModuleStop) {
          ProcessEvent(msg);
        } else {
          event_message_release(&msg);
          LOG_I("[NetActor id: {}] module shutdown...", actor_id_);
          co_return ActorExitState::kExitSuccess;
        }
      }
      event_message_release(&msg);
      co_await mailbox->Wait();
    }
  } catch (const std::exception &e) {
    // customize error handle
    this->error_handle(e);
    // 记录协程崩溃时所处理的事件消息类型
    msg->type_ = EventType::kEventCrashReport;
    msg->set(std::format("[NetActor id: {}] event_type: {}, error: {}",
                         actor_id_, event_type_to_string(msg->type_),
                         e.what()));
    parent_mailbox_->Send(msg);
    co_return ActorExitState::kExitFailure;
  }
}

} // namespace ac
