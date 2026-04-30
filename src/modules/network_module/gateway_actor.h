// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_NETWORK_MODULE_GATEWAY_ACTOR_H_
#define AC_NETWORK_MODULE_GATEWAY_ACTOR_H_

#include "actor_module.h"

namespace ac {

class GateWayActor : public ActorModule {
public:
  GateWayActor(std::size_t actor_id, MailBoxPtr parent_mailbox);
  ~GateWayActor() = default;

  void Init(MailBoxPtr &mailbox) override;

  void Uninit(MailBoxPtr &mailbox) override;

  void ProcessEvent(EventMessage *message) override;

  void error_handle(const std::exception &e) override;

private:
  int fd_{-1};
};

extern "C" {
ActorModule *CreateModule(std::size_t actor_id, MailBoxPtr parent_mailbox);

void DestroyModule(ActorModule *module);
}

}; // namespace ac

#endif
