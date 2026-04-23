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
  std::string module_name;
  std::function<LaunchTask(std::size_t, MailBoxPtr)> creator;
};

class ActorManager {
public:
  ActorManager();
  ~ActorManager();

  void EventLoop();

  LaunchTask RunCoroutine();

  void ReloadModule(std::string_view module_name);

  void Shutdown();

private:
  auto __generate_creator(std::string path);

  void __load_modules();

  bool __search_module_and_remove(std::string_view module_name);

  void __reload_module(std::size_t module_id, std::string_view module_name);

  void __reload_all_modules();

  bool is_running_{false};
  bool reload_all_flag_{false};
  std::latch main_latch_{1};

  std::unordered_map<std::size_t, ActorMetaData> childrens_;
  std::vector<std::size_t> pedding_reload_;

  MailBoxPtr mailbox_;
};

} // namespace ac

#endif
