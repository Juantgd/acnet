// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_manager.h"

#include <algorithm>

#include <dlfcn.h>

#include "actor_module.h"
#include "actor_scheduler.h"
#include "helper.h"
#include "plugin_manager.h"

namespace ac {

ActorManager::ActorManager() { mailbox_ = std::make_shared<MailBox>(); }

ActorManager::~ActorManager() { Shutdown(); }

auto ActorManager::__generate_creator(std::string path) {
  return [this, path = std::move(path)](std::size_t actor_id,
                                        MailBoxPtr mailbox) -> LaunchTask {
    return __launch_actor(std::string(path), actor_id, std::move(mailbox));
  };
}

LaunchTask ActorManager::__launch_actor(std::string path, std::size_t actor_id,
                                        MailBoxPtr mailbox) {
  ActorModule *actor = nullptr;
  void *handle = dlopen(path.c_str(), RTLD_LAZY);
  if (handle) {
    auto create_func = (CreateModuleFunc)dlsym(handle, "CreateModule");
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
      LOG_E("Cannot find the CreateModule function symbol. error: {}",
            dlsym_error);
      dlclose(handle);
      goto creator_exit;
    }
    auto destroy_func = (DestroyModuleFunc)dlsym(handle, "DestroyModule");
    dlsym_error = dlerror();
    if (dlsym_error) {
      LOG_E("Cannot find the DestroyModule function symbol. error: {}",
            dlsym_error);
      dlclose(handle);
      goto creator_exit;
    }
    try {
      actor = create_func(actor_id, this->mailbox_);
      if (actor) {
        actor->Init(mailbox);
        co_await actor->RunCoroutine(mailbox);
      } else {
        throw std::runtime_error("CreateModule() failed.");
      }
    } catch (const std::exception &e) {
      if (actor) {
        actor->CrashReport(e);
      } else {
        LOG_E("unable to create module, error: {}", e.what());
      }
    }
    if (actor) {
      actor->Uninit(mailbox);
      destroy_func(&actor);
    }
    dlclose(handle);
  } else {
    LOG_E("Failed to load library: {}, error: {}", path, dlerror());
  }
creator_exit:
  // send exited message to parent mailbox
  EventMessage *msg =
      new EventMessage(1, EventType::kEventModuleExited, actor_id);
  this->mailbox_->Send(msg);
  co_await std::suspend_never();
}

void ActorManager::__load_modules() {
  // loading configures and mount module
  const auto &config = PluginManager::Instance().GetConfigInfo();
  if (!config.modules.empty()) {
    std::vector<std::coroutine_handle<>> tasks;
    for (const auto &mod : config.modules) {
      std::string path =
          std::format("{}/lib{}.so", config.library_path, mod.library_name);
      // print module information
      LOG_I("=========================================");
      LOG_I("module_name: {}", mod.module_name);
      LOG_I("module_library_path: {}", path);
      LOG_I("=========================================");

      auto creator_func = __generate_creator(std::move(path));

      ActorMetaData metadata{.actor_id = generate_unique_id(),
                             .mailbox = std::make_shared<MailBox>(),
                             .module_name = mod.module_name,
                             .creator = std::move(creator_func)};
      auto module_task = metadata.creator(metadata.actor_id, metadata.mailbox);

      childrens_.emplace(metadata.actor_id, std::move(metadata));

      tasks.push_back(module_task.handle_);
    }
    ActorScheduler::Instance().EnqueueBatch(tasks);
  }
}

void ActorManager::EventLoop() {
  is_running_ = true;
  auto event_loop = RunCoroutine();
  ActorScheduler::Instance().Enqueue(event_loop.handle_);

  __load_modules();

  // blocking until event loop coroutine done.
  main_latch_.wait();
  ActorScheduler::Instance().Shutdown();
  LOG_I("=========Server Shutdown=========");
}

LaunchTask ActorManager::RunCoroutine() {
  EventMessage *msg = nullptr;
  while (true) {
    msg = co_await mailbox_->Receive();
    if (!msg && !mailbox_->try_receive(&msg)) [[unlikely]] {
      continue;
    }
    switch (msg->type_) {
    case EventType::kEventCmdExit: {
      event_message_release(&msg);
      goto coro_exit;
    }
    case EventType::kEventCmdModuleReload: {
      __handle_cmd_reload(msg);
      break;
    }
    case EventType::kEventCmdModuleRemove: {
      __handle_cmd_remove(msg);
      break;
    }
    case EventType::kEventCrashReport: {
      __handle_event_crash(msg);
      break;
    }
    case EventType::kEventModuleExited: {
      __handle_event_exit(msg);
      break;
    }
    default:
      LOG_W("Unsupport event type: {}", static_cast<int>(msg->type_));
    }
    event_message_release(&msg);
  }
coro_exit:
  main_latch_.count_down();
}

void ActorManager::__reload_module(std::size_t module_id,
                                   std::string_view module_name) {
  const auto &config = PluginManager::Instance().GetConfigInfo();
  if (!config.modules.empty()) {
    for (const auto &mod : config.modules) {
      if (module_name == mod.module_name) {
        std::string path =
            std::format("{}/lib{}.so", config.library_path, mod.library_name);
        // print module information
        LOG_I("=========================================");
        LOG_I("module_name: {}", mod.module_name);
        LOG_I("module_library_path: {}", path);
        LOG_I("=========================================");

        auto creator_func = __generate_creator(std::move(path));

        if (module_id == 0) {
          ActorMetaData metadata{.actor_id = generate_unique_id(),
                                 .mailbox = std::make_shared<MailBox>(),
                                 .module_name = mod.module_name,
                                 .creator = std::move(creator_func)};
          LOG_I("module not load, create module: {}, id: {}", module_name,
                metadata.actor_id);
          auto module_task =
              metadata.creator(metadata.actor_id, metadata.mailbox);
          childrens_.emplace(metadata.actor_id, std::move(metadata));
          ActorScheduler::Instance().Enqueue(module_task.handle_);
        } else {
          auto it = childrens_.find(module_id);
          if (it == childrens_.end()) {
            LOG_E("module not found, module_name: {}", module_name);
            break;
          }
          it->second.creator = std::move(creator_func);
          auto module_task =
              it->second.creator(it->second.actor_id, it->second.mailbox);
          ActorScheduler::Instance().Enqueue(module_task.handle_);
        }
        break;
      }
    }
  }
}

void ActorManager::__reload_all_modules() {
  const auto &config = PluginManager::Instance().GetConfigInfo();
  if (!config.modules.empty()) {
    std::vector<std::coroutine_handle<>> tasks;
    for (const auto &mod : config.modules) {
      std::string path =
          std::format("{}/lib{}.so", config.library_path, mod.library_name);
      // print module information
      LOG_I("=========================================");
      LOG_I("module_name: {}", mod.module_name);
      LOG_I("module_library_path: {}", path);
      LOG_I("=========================================");

      auto creator_func = __generate_creator(std::move(path));

      for (auto it = childrens_.begin(); it != childrens_.end(); ++it) {
        if (it->second.module_name == mod.module_name) {
          it->second.creator = std::move(creator_func);
          auto module_task =
              it->second.creator(it->second.actor_id, it->second.mailbox);
          tasks.push_back(module_task.handle_);
        } else {
          ActorMetaData metadata{.actor_id = generate_unique_id(),
                                 .mailbox = std::make_shared<MailBox>(),
                                 .module_name = mod.module_name,
                                 .creator = std::move(creator_func)};
          auto module_task =
              metadata.creator(metadata.actor_id, metadata.mailbox);
          childrens_.emplace(metadata.actor_id, std::move(metadata));
          tasks.push_back(module_task.handle_);
        }
      }
    }
    ActorScheduler::Instance().EnqueueBatch(tasks);
  }
}

void ActorManager::ReloadModule(std::string_view module_name) {
  if (module_name == "all") {
    LOG_I("try to reload all modules");
  } else {
    LOG_I("try to reload module: {}", module_name);
  }
  EventMessage *msg =
      new EventMessage(1, ac::EventType::kEventCmdModuleReload, 0);
  msg->set(std::string(module_name));
  mailbox_->Send(msg);
}

void ActorManager::RemoveModule(std::string_view module_name) {
  LOG_I("try to remove module: {}", module_name);
  EventMessage *msg =
      new EventMessage(1, ac::EventType::kEventCmdModuleRemove, 0);
  msg->set(std::string(module_name));
  mailbox_->Send(msg);
}

void ActorManager::Shutdown() {
  if (is_running_) {
    is_running_ = false;
    EventMessage *msg = new EventMessage(1, EventType::kEventCmdExit, 0);
    mailbox_->Send(msg);
  }
}

bool ActorManager::__search_module_and_remove(std::string_view module_name) {
  reload_all_flag_ = module_name == "all" ? true : false;
  EventMessage *message =
      new EventMessage(reload_all_flag_ ? std::max(childrens_.size(), 1UL) : 1,
                       EventType::kEventCmdModuleStop, 0);
  for (auto it = childrens_.begin(); it != childrens_.end(); ++it) {
    if (reload_all_flag_) {
      it->second.mailbox->Send(message);
      pending_reload_.push_back(it->first);
    } else if (it->second.module_name == module_name) {
      it->second.mailbox->Send(message);
      pending_reload_.push_back(it->first);
      return true;
    }
  }
  if (reload_all_flag_) {
    if (pending_reload_.empty()) {
      event_message_release(&message);
      __load_modules();
    }
    return true;
  }
  event_message_release(&message);
  return false;
}

void ActorManager::__handle_cmd_reload(EventMessage *message) {
  PluginManager::Instance().UpdateConfig();
  if (!__search_module_and_remove(message->get<std::string>())) {
    // module not found
    __reload_module(0, message->get<std::string>());
  }
}

void ActorManager::__handle_cmd_remove(EventMessage *message) {
  for (auto it = childrens_.begin(); it != childrens_.end(); ++it) {
    if (it->second.module_name == message->get<std::string>()) {
      LOG_I("found the module: {}, prepare remove", it->second.module_name);
      EventMessage *msg =
          new EventMessage(1, EventType::kEventCmdModuleStop, 0);
      it->second.mailbox->Send(msg);
      return;
    }
  }
  LOG_W("module: {} not found", message->get<std::string>());
}

void ActorManager::__handle_event_exit(EventMessage *message) {
  bool reload_flag = false;
  if (!pending_reload_.empty()) {
    LOG_D("pedding reload: {}", pending_reload_);
    for (std::size_t i = 0; i != pending_reload_.size(); ++i) {
      if (message->sender_id_ == pending_reload_[i]) {
        std::swap(pending_reload_.back(), pending_reload_[i]);
        pending_reload_.pop_back();
        reload_flag = true;
        break;
      }
    }
  }
  auto it = childrens_.find(message->sender_id_);
  if (it == childrens_.end()) [[unlikely]] {
    LOG_E("failed to unmount module");
    return;
  }
  if (reload_flag) {
    pending_restart_.erase(message->sender_id_);
    // reload module, load new library, reuse mailbox
    if (reload_all_flag_) {
      if (pending_reload_.empty()) {
        LOG_I("all modules reloading...");
        __reload_all_modules();
      }
    } else {
      LOG_I("module: {} reloading, id: {}", it->second.module_name,
            message->sender_id_);
      __reload_module(message->sender_id_, it->second.module_name);
    }
  } else if (pending_restart_.erase(message->sender_id_) != 0) {
    LOG_I("module: {} crashed, restarting, id: {}", it->second.module_name,
          message->sender_id_);
    auto task = it->second.creator(it->second.actor_id, it->second.mailbox);
    ActorScheduler::Instance().Enqueue(task.handle_);
  } else {
    // not reload, remove module
    LOG_I("module: {} exited, id: {}, modules: {}", it->second.module_name,
          message->sender_id_, childrens_.size() - 1);
    childrens_.erase(it);
  }
}

void ActorManager::__handle_event_crash(EventMessage *message) {
  auto it = childrens_.find(message->sender_id_);
  if (it != childrens_.end()) {
    LOG_E("received module crash report, message: {}",
          message->get<std::string>());
    pending_restart_.insert(message->sender_id_);
  } else {
    LOG_W("Actor Not found, actor id: {}", message->sender_id_);
  }
}

} // namespace ac
