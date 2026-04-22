// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_manager.h"

#include <dlfcn.h>

#include "actor_module.h"
#include "actor_scheduler.h"
#include "helper.h"
#include "plugin_manager.h"

namespace ac {

ActorManager::ActorManager() { mailbox_ = std::make_shared<MailBox>(); }

ActorManager::~ActorManager() { Shutdown(); }

void ActorManager::__load_module(std::string_view module_name) {
  // loading configures and mount module
  const auto &config = PluginManager::Instance().GetConfigInfo();
  if (!config.modules.empty()) {
    std::vector<std::coroutine_handle<>> tasks;
    for (const auto &mod : config.modules) {
      if (!module_name.empty() && module_name != mod.module_name) {
        continue;
      }
      std::string path =
          std::format("{}/lib{}.so", config.library_path, mod.library_name);
      // print module information
      LOG_I("=========================================");
      LOG_I("module_name: {}", mod.module_name);
      LOG_I("module_library_path: {}", path);
      LOG_I("=========================================");

      auto creator =
          [=, this, path = std::move(path)](std::size_t actor_id,
                                            MailBoxPtr mailbox) -> LaunchTask {
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
            co_await actor->RunCoroutine(std::move(mailbox));
          } catch (const std::exception &e) {
            if (actor) {
              actor->CrashReport(e);
            } else {
              LOG_E("unable to create module, error: {}", e.what());
            }
          }
          if (actor)
            destroy_func(actor);
          dlclose(handle);
        } else {
          LOG_E("Failed to load library: {}, error: {}", path, dlerror());
        }
      creator_exit:
        LOG_I("Module Exited.");
        // send exited message to parent mailbox
        EventMessage *msg =
            new EventMessage(1, EventType::kEventModuleExited, actor_id);
        this->mailbox_->Send(msg);
        co_await std::suspend_never();
      };

      ActorMetaData metadata{.actor_id = generate_unique_id(),
                             .mailbox = std::make_shared<MailBox>(),
                             .module_name = mod.module_name,
                             .creator = std::move(creator)};
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

  __load_module();

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
    case EventType::kEventModuleReload: {
      if (!__search_module_and_remove(msg->get<std::string>())) {
        // module not found
        __reload_module(msg->get<std::string>());
      }
      break;
    }
    case EventType::kEventCrashReport: {
      auto it = childrens_.find(msg->sender_id_);
      if (it != childrens_.end()) {
        LOG_E("received module crash report, message: {}",
              msg->get<std::string>());
        ActorMetaData &actor_data = it->second;
        auto task = actor_data.creator(actor_data.actor_id, actor_data.mailbox);
        ActorScheduler::Instance().Enqueue(task.handle_);
      } else {
        LOG_W("Actor Not found, actor id: {}", msg->sender_id_);
      }
      break;
    }
    case EventType::kEventModuleExited: {
      bool reload_flag = false;
      if (!pedding_reload_.empty()) {
        for (std::size_t i = 0; i != pedding_reload_.size(); ++i) {
          if (msg->sender_id_ == pedding_reload_[i]) {
            std::swap(pedding_reload_.back(), pedding_reload_[i]);
            pedding_reload_.pop_back();
            reload_flag = true;
            break;
          }
        }
      }
      auto it = childrens_.find(msg->sender_id_);
      if (it == childrens_.end()) {
        LOG_E("failed to unmount module");
        break;
      }
      std::string module_name = std::move(it->second.module_name);
      childrens_.erase(it);
      LOG_I("module: {} exited, id: {}, modules: {}", module_name,
            msg->sender_id_, childrens_.size());
      if (reload_flag) {
        LOG_W("module are exited, now reload module");
        __reload_module(module_name);
      }
      break;
    }
    case EventType::kEventExited: {
      event_message_release(msg);
      goto coro_exit;
    }
    default:
      LOG_W("Unsupport event type: {}", static_cast<int>(msg->type_));
    }
    event_message_release(msg);
  }
coro_exit:
  main_latch_.count_down();
}

void ActorManager::__reload_module(std::string_view module_name) {
  PluginManager::Instance().UpdateConfig();
  __load_module(module_name);
}

bool ActorManager::__search_module_and_remove(std::string_view module_name) {
  for (auto it = childrens_.begin(); it != childrens_.end(); ++it) {
    if (it->second.module_name == module_name) {
      EventMessage *message =
          new EventMessage(1, EventType::kEventModuleStop, 0);
      it->second.mailbox->Send(message);
      pedding_reload_.push_back(it->first);
      return true;
    }
  }
  return false;
}

void ActorManager::ReloadModule(std::string_view module_name) {
  LOG_I("try to reload module: {}", module_name);
  EventMessage *msg = new EventMessage(1, ac::EventType::kEventModuleReload, 0);
  msg->set(std::string(module_name));
  mailbox_->Send(msg);
}

void ActorManager::Shutdown() {
  if (is_running_) {
    is_running_ = false;
    EventMessage *msg = new EventMessage(1, EventType::kEventExited, 0);
    mailbox_->Send(msg);
  }
}

} // namespace ac
