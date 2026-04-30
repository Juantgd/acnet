// Copyright (c) 2026 juantgd. All Rights Reserved.

#include "actor_scheduler.h"

#include <atomic>
#include <random>
#include <sched.h>
#include <thread>

namespace ac {

thread_local Worker *tls_worker{nullptr};

ActorScheduler::ActorScheduler() {
  thread_entries_ = std::thread::hardware_concurrency();
  running_.store(true, std::memory_order_relaxed);
  worker_threads_ = new pthread_t[thread_entries_];
  workers_ =
      static_cast<Worker *>(::operator new(sizeof(Worker) * thread_entries_));
  pthread_barrier_init(&thread_barrier_, NULL, thread_entries_ + 1);
  cpu_set_t cpuset;
  for (unsigned int i = 0; i != thread_entries_; ++i) {
    new (&workers_[i]) Worker(i, this);
    pthread_create(&worker_threads_[i], NULL, scheduler_thread, &workers_[i]);
    // 调度线程绑定核心
    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);
    pthread_setaffinity_np(worker_threads_[i], sizeof(cpuset), &cpuset);
  }
  // 等待所有工作线程初始化完毕
  pthread_barrier_wait(&thread_barrier_);
}

ActorScheduler::~ActorScheduler() {
  Shutdown();
  for (unsigned int i = 0; i != thread_entries_; ++i) {
    pthread_join(worker_threads_[i], NULL);
    workers_[i].~Worker();
  }
  ::operator delete(workers_);
  delete[] worker_threads_;
  pthread_barrier_destroy(&thread_barrier_);
}

void ActorScheduler::Enqueue(const std::coroutine_handle<> &handle) {
  // worker内提交优先进入本地队列；出现积压时唤醒空闲worker重新搜索任务。
  if (tls_worker && tls_worker->local_queue.push(handle)) {
    if (tls_worker->local_queue.approx_size() > 1) {
      global_queue_.notify_one_idle();
    }
    return;
  }
  // 外部线程提交或本地队列已满时进入全局注入队列。
  global_queue_.push(handle);
}

void ActorScheduler::EnqueueBatch(
    const std::vector<std::coroutine_handle<>> &tasks) {
  global_queue_.push_batch(tasks);
}

// 本地调度线程入口函数
void *ActorScheduler::scheduler_thread(void *arg) {
  tls_worker = static_cast<Worker *>(arg);
  ActorScheduler *sched = tls_worker->sched;
  std::mt19937 rng(tls_worker->worker_id);
  char thread_name[16];
  snprintf(thread_name, 16, "sched-worker-%u", tls_worker->worker_id);
  pthread_setname_np(pthread_self(), thread_name);
  // 线程屏障,等待其他线程初始化完毕
  pthread_barrier_wait(&sched->thread_barrier_);

  uint32_t tick = 0;

  auto try_get_task = [&]() {
    std::coroutine_handle<> task{nullptr};

    // 保留周期性检查全局队列，避免本地队列持续繁忙时饿死外部提交任务。
    if (++tick % 61 == 0) {
      task = sched->global_queue_.try_pop();
      if (task) {
        return task;
      }
    }

    tls_worker->local_queue.pop(task);
    if (task) {
      return task;
    }

    task = sched->global_queue_.try_pop();
    if (task) {
      return task;
    }

    uint32_t target_id = static_cast<uint32_t>(rng() % sched->thread_entries_);
    for (uint32_t i = 0; i != sched->thread_entries_; ++i) {
      if (target_id != tls_worker->worker_id) {
        StealState result = sched->workers_[target_id].local_queue.steal(task);
        if (result == StealState::kSuccess) {
          if (sched->workers_[target_id].local_queue.approx_size() > 1) {
            sched->global_queue_.notify_one_idle();
          }
          LOG_D("worker steal success, task: {}", task.address());
          return task;
        }
      }
      target_id = (target_id + 1) % sched->thread_entries_;
    }

    return task;
  };

  while (sched->is_running()) {
    std::coroutine_handle<> task = try_get_task();
    if (!task) {
      std::size_t observed_epoch = sched->global_queue_.prepare_park();
      task = try_get_task();
      if (!task) {
        sched->global_queue_.park(observed_epoch);
        continue;
      }
      sched->global_queue_.cancel_park();
    }
    if (task && !task.done()) {
      LOG_D("got the task, task: {}", task.address());
      task.resume();
    }
  }
  // 退出调度线程
  return NULL;
}

} // namespace ac
