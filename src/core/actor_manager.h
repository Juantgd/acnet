// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_ACTOR_MANAGER_H_
#define AC_INCLUDE_ACTOR_MANAGER_H_

#include <functional>
#include <latch>
#include <string_view>
#include <unordered_map>

#include "mail_box.h"
#include "task.h"

namespace ac {

struct ActorMetaData {
  std::size_t actor_id;
  MailBoxPtr mailbox;
  std::function<LaunchTask(std::size_t, MailBoxPtr)> creator;
};

class ActorManager {
public:
  ActorManager();
  ~ActorManager();

  void LoadModules();

  // TODO: implementation of module reload
  void ReloadModule(std::string_view module_name);

  void EventLoop();

  LaunchTask RunCoroutine();

  void Shutdown();

private:
  bool is_running_{false};
  std::latch main_latch_{1};

  std::unordered_map<std::size_t, ActorMetaData> childrens_;
  MailBoxPtr mailbox_;
};

} // namespace ac

#endif
