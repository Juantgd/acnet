// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "gateway_actor.h"

#include "event_bus.h"

namespace ac {

GateWayActor::GateWayActor(std::size_t actor_id, MailBoxPtr parent_mailbox)
    : ActorModule(actor_id, parent_mailbox) {}

void GateWayActor::Init(MailBoxPtr &mailbox) {
  ActorEventBus::Instance().Subscribe(EventType::kEventCmdModuleStop, mailbox);
}

void GateWayActor::Uninit(MailBoxPtr &mailbox) {
  ActorEventBus::Instance().Unsubscribe(EventType::kEventCmdModuleStop,
                                        mailbox);
}

Task<void> GateWayActor::RunCoroutine(MailBoxPtr &mailbox) {
  LOG_I("[NetActor id: {}] module starting...", actor_id_);
  while (true) {
    msg_ = co_await mailbox->Receive();
    if (!msg_ && !mailbox->try_receive(&msg_)) [[unlikely]] {
      continue;
    }
    LOG_I("[NetActor id: {}] received a event message...", actor_id_);
    switch (msg_->type_) {
    case EventType::kEventCmdModuleStop: {
      event_message_release(&msg_);
      goto coro_exit;
      break;
    }
    default:
      LOG_W("[NetActor id: {}] unsupported event message type: {}", actor_id_,
            static_cast<int>(msg_->type_));
      break;
    }
    event_message_release(&msg_);
  }
coro_exit:
  LOG_I("[NetActor id: {}] module shutdown...", actor_id_);
}

extern "C" {
ActorModule *CreateModule(std::size_t actor_id, MailBoxPtr parent_mailbox) {
  return new GateWayActor(actor_id, parent_mailbox);
}

void DestroyModule(ActorModule **module) {
  if (*module) {
    delete *module;
    *module = nullptr;
  }
}
}

} // namespace ac
