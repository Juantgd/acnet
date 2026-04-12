#ifndef AC_CORE_ACTOR_SCHEDULER_H_
#define AC_CORE_ACTOR_SCHEDULER_H_

#include <atomic>

#include <pthread.h>

#include "queue.h"

namespace ac {

template class WorkStealingQueue<kWorkStealingQueueSize>;

class ActorScheduler;

struct Worker {
  Worker(uint32_t id, ActorScheduler *scheduler)
      : worker_id(id), sched(scheduler), local_queue() {}
  ~Worker() = default;
  uint32_t worker_id;
  ActorScheduler *sched;
  WorkStealingQueue<kWorkStealingQueueSize> local_queue;
};

// 协程调度器
class ActorScheduler {
public:
  ~ActorScheduler();

  inline static ActorScheduler &Instance() {
    static ActorScheduler sched;
    return sched;
  }

  void Enqueue(std::coroutine_handle<> handle);

  inline bool is_running() const {
    return running_.load(std::memory_order_relaxed);
  }

private:
  friend class ActorManager;

  ActorScheduler();

  void Shutdown() { running_.store(false, std::memory_order_relaxed); }

  static void *scheduler_thread(void *arg);
  pthread_t *worker_threads_;
  Worker *workers_;
  std::atomic<bool> running_{false};
  uint32_t thread_entries_;
  pthread_barrier_t thread_barrier_;

  GlobalQueue global_queue_;
};

extern thread_local Worker *tls_worker;

} // namespace ac

#endif
