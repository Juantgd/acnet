// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_ACTOR_MODULE_H_
#define AC_INCLUDE_ACTOR_MODULE_H_

#include <cstdint>

#include "mail_box.h"
#include "task.h"

namespace ac {

enum class ActorExitState : uint8_t { kExitSuccess = 0, kExitFailure };

// 模块类与执行协程分离,即使执行协程遇到错误崩溃了
// 监管Actor也可以重新通过模块类拉起
class ActorModule {
public:
  ActorModule(std::size_t id, MailBoxPtr parent_mailbox);
  virtual ~ActorModule() = default;

  virtual void Init([[maybe_unused]] MailBoxPtr &mailbox) {}

  virtual void Uninit([[maybe_unused]] MailBoxPtr &mailbox) {}

  virtual void ProcessEvent(EventMessage *message) = 0;

  Task<ActorExitState> RunCoroutine(MailBoxPtr &mailbox);

  inline std::size_t get_id() const noexcept { return actor_id_; }

protected:
  virtual void error_handle([[maybe_unused]] const std::exception &e) {}

  // Actor唯一标识
  std::size_t actor_id_;
  // parent mailbox
  MailBoxPtr parent_mailbox_;
};

typedef ActorModule *(*CreateModuleFunc)(std::size_t id,
                                         MailBoxPtr parent_mailbox);

typedef void (*DestroyModuleFunc)(ActorModule *module);

} // namespace ac

#endif
