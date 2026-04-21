// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_module.h"

namespace ac {

ActorModule::ActorModule(std::size_t id, MailBoxPtr parent_mailbox)
    : actor_id_(id), parent_mailbox_(parent_mailbox) {}

void ActorModule::CrashReport(const std::exception &e) {
  // error handle
  this->error_handle(e);
  // report crash to the parent mailbox
  EventMessage *msg =
      new EventMessage(1, EventType::kEventCrashReport, actor_id_);
  msg->set(std::format("encounter error: {}", e.what()));
  parent_mailbox_->Send(msg);
}

} // namespace ac
