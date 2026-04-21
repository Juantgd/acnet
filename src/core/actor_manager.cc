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

void ActorManager::LoadModules() {
  // loading configures and mount module
  auto config = PluginManager::Instance().GetConfigInfo();
  std::vector<std::coroutine_handle<>> tasks(config.modules.size());

  if (!config.modules.empty()) {
    for (auto mod : config.modules) {
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
        co_await std::suspend_never();
      };

      ActorMetaData metadata{.actor_id = generate_unique_id(),
                             .mailbox = std::make_shared<MailBox>(),
                             .creator = std::move(creator)};
      auto module_task = metadata.creator(metadata.actor_id, metadata.mailbox);
      childrens_.emplace(metadata.actor_id, std::move(metadata));

      tasks.push_back(module_task.handle_);
    }
  }

  for (auto task : tasks) {
    ActorScheduler::Instance().Enqueue(task);
  }
}

void ActorManager::EventLoop() {
  is_running_ = true;
  auto event_loop = RunCoroutine();
  ActorScheduler::Instance().Enqueue(event_loop.handle_);

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
      ReloadModule(msg->get<std::string>());
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

void ActorManager::ReloadModule(std::string_view module_name) {}

void ActorManager::Shutdown() {
  if (is_running_) {
    is_running_ = false;
    EventMessage *msg = new EventMessage(1, EventType::kEventExited, 0);
    mailbox_->Send(msg);
  }
}

} // namespace ac
