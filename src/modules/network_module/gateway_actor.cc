// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "gateway_actor.h"

namespace ac {

GateWayActor::GateWayActor(std::size_t actor_id, MailBoxPtr parent_mailbox)
    : ActorModule(actor_id, parent_mailbox) {}

Task<void> GateWayActor::RunCoroutine(MailBoxPtr mailbox) {
  LOG_I("[NetActor] module starting...");
  EventMessage *msg = nullptr;
  while (true) {
    msg = co_await mailbox->Receive();
    if (!msg && !mailbox->try_receive(&msg)) [[unlikely]] {
      continue;
    }
    LOG_I("[NetActor] received a event message...");
    switch (msg->type_) {
    case EventType::kEventModuleStop: {
      event_message_release(msg);
      goto coro_exit;
      break;
    }
    default:
      LOG_W("[NetActor] unsupported event message type: {}",
            static_cast<int>(msg->type_));
      break;
    }
    event_message_release(msg);
  }
coro_exit:
  LOG_I("[NetActor] module shutdown...");
}

extern "C" {
ActorModule *CreateModule(std::size_t actor_id, MailBoxPtr parent_mailbox) {
  return new GateWayActor(actor_id, parent_mailbox);
}

void DestroyModule(ActorModule *module) {
  if (module) {
    delete module;
    module = nullptr;
  }
}
}

} // namespace ac
