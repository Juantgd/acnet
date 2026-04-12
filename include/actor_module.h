#ifndef AC_INCLUDE_ACTOR_MODULE_H_
#define AC_INCLUDE_ACTOR_MODULE_H_

#include "mail_box.h"
#include "task.h"

namespace ac {

// 模块类与执行协程分离,即使执行协程遇到错误崩溃了
// 监管Actor也可以重新通过模块类拉起
class ActorModule {
public:
  // 模块初始化时,向事件总线订阅相关事件消息
  ActorModule(std::size_t id, MailBoxPtr mail_box);
  // 模块析构时,取消相关的订阅
  virtual ~ActorModule() = default;

  virtual Task<void> RunCoroutine(MailBoxPtr mailbox) = 0;

  inline const std::size_t get_id() const noexcept { return actor_id_; }

protected:
  // Actor唯一标识
  std::size_t actor_id_;
  // parent mailbox
  MailBoxPtr parent_mailbox_;
};

} // namespace ac

#endif
