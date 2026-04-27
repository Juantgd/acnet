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

void DestroyModule(ActorModule **module) {
  if (*module) {
    delete *module;
    *module = nullptr;
  }
}
}

} // namespace ac
