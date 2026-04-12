#include "actor_manager.h"

#include "actor_module.h"
#include "actor_scheduler.h"
#include "helper.h"

namespace ac {

ActorManager::ActorManager() { mailbox_ = std::make_shared<MailBox>(512); }

ActorManager::~ActorManager() { stop_ = true; }

void ActorManager::LoadModules() {
  // loading configures and mount module
  auto creator = [=, this](std::size_t actor_id,
                           MailBoxPtr mailbox) -> LaunchTask {
    ActorModule *actor = nullptr;
    try {
      // TODO: replace real Actor create function
      // actor = new ActorEventBus(actor_id, mailbox_);
      co_await actor->RunCoroutine(std::move(mailbox));
      if (actor)
        delete actor;
    } catch (const std::exception &e) {
      EventMessage *msg =
          new EventMessage(1, EventType::kEventCrashReport, actor_id);
      msg->set(std::format("encounter error: {}", e.what()));
      if (actor)
        delete actor;
      mailbox_->Send(msg);
    }
  };
  ActorMetaData metadata{.actor_id = generate_unique_id(),
                         .mailbox = std::make_shared<MailBox>(),
                         .creator = creator};
  auto module_task = creator(metadata.actor_id, metadata.mailbox);
  childrens_.emplace(metadata.actor_id, std::move(metadata));

  ActorScheduler::Instance().Enqueue(module_task.handle);
}

void ActorManager::EventLoop() {
  stop_ = false;
  auto event_loop = RunCoroutine();
  ActorScheduler::Instance().Enqueue(event_loop.handle);

  // blocking until event loop coroutine done.
  main_latch_.wait();
  LOG_I("=========Server Shutdown=========");
}

LaunchTask ActorManager::RunCoroutine() {
  EventMessage *msg = nullptr;
  while (!stop_) {
    msg = co_await mailbox_->Receive();
    if (!msg && !mailbox_->try_receive(&msg)) [[unlikely]] {
      continue;
    }
    switch (msg->type) {
    case EventType::kEventModuleReload: {
      ReloadModule("test");
      break;
    }
    case EventType::kEventCrashReport: {
      auto it = childrens_.find(msg->sender_id);
      if (it != childrens_.end()) {
        ActorMetaData &actor_data = it->second;
        auto task = actor_data.creator(actor_data.actor_id, actor_data.mailbox);
        ActorScheduler::Instance().Enqueue(task.handle);
      } else {
        LOG_W("Actor Not found, actor id: {}", msg->sender_id);
      }
      break;
    }
    default:
      LOG_W("Unsupport event type: {}", static_cast<int>(msg->type));
    }
    event_message_release(msg);
  }
  ActorScheduler::Instance().Shutdown();
  main_latch_.count_down();
}

void ActorManager::Shutdown() { stop_ = true; }

} // namespace ac
