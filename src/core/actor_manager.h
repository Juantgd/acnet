// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_ACTOR_MANAGER_H_
#define AC_INCLUDE_ACTOR_MANAGER_H_

#include <functional>
#include <latch>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "mail_box.h"
#include "task.h"

namespace ac {

struct LoadedModule;

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

  void RemoveModule(std::string_view module_name);

  void Shutdown();

private:
  std::function<LaunchTask(std::size_t, MailBoxPtr)>
  __generate_creator(std::string path);

  LaunchTask __launch_actor(std::shared_ptr<LoadedModule> module,
                            std::size_t actor_id, MailBoxPtr mailbox);

  void __load_modules();

  bool __search_module_and_remove(std::string_view module_name);

  void __reload_module(std::size_t module_id, std::string_view module_name);

  void __reload_all_modules();

  void __restart_module(ActorMetaData &metadata);

  bool __handle_cmd_exit([[maybe_unused]] EventMessage *message);
  void __handle_cmd_reload(EventMessage *message);
  void __handle_cmd_remove(EventMessage *message);
  bool __handle_event_exit(EventMessage *message);
  void __handle_event_crash(EventMessage *message);

  bool is_running_{false};
  bool reload_all_flag_{false};
  std::latch main_latch_{1};

  std::unordered_map<std::size_t, ActorMetaData> childrens_;
  std::unordered_set<std::size_t> pending_reload_;
  std::unordered_set<std::size_t> pending_stop_;
  std::unordered_set<std::size_t> pending_restart_;

  MailBoxPtr mailbox_;
};

} // namespace ac

#endif
