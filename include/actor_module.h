// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_ACTOR_MODULE_H_
#define AC_INCLUDE_ACTOR_MODULE_H_

#include "mail_box.h"
#include "task.h"

namespace ac {

// 模块类与执行协程分离,即使执行协程遇到错误崩溃了
// 监管Actor也可以重新通过模块类拉起
class ActorModule {
public:
  ActorModule(std::size_t id, MailBoxPtr parent_mailbox);
  virtual ~ActorModule();

  virtual void Init(MailBoxPtr &) {}

  virtual void Uninit(MailBoxPtr &) {}

  virtual Task<void> RunCoroutine(MailBoxPtr &mailbox) = 0;

  inline std::size_t get_id() const noexcept { return actor_id_; }

  void CrashReport(const std::exception &e);

protected:
  virtual void error_handle(const std::exception &) {};

  // Actor唯一标识
  std::size_t actor_id_;
  // parent mailbox
  MailBoxPtr parent_mailbox_;
  // 当前处理的EventMessage
  EventMessage *msg_{nullptr};
};

typedef ActorModule *(*CreateModuleFunc)(std::size_t id,
                                         MailBoxPtr parent_mailbox);

typedef void (*DestroyModuleFunc)(ActorModule *module);

} // namespace ac

#endif
