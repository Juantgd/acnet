// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "gateway_actor.h"

#include <chrono>

#include "event_bus.h"

using namespace std::chrono_literals;

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

void GateWayActor::error_handle([[maybe_unused]] const std::exception &e) {
  std::this_thread::sleep_for(5s);
}

void GateWayActor::ProcessEvent(EventMessage *message) {
  switch (message->type_) {
  case EventType::kEventNone: {
    break;
  }
  default: {
  }
  }
}

extern "C" {
ActorModule *CreateModule(std::size_t actor_id, MailBoxPtr parent_mailbox) {
  return new GateWayActor(actor_id, parent_mailbox);
}

void DestroyModule(ActorModule *module) { delete module; }
}

} // namespace ac
