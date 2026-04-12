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
  running_.store(false, std::memory_order_relaxed);
  global_queue_.stop();
  for (unsigned int i = 0; i != thread_entries_; ++i) {
    pthread_join(worker_threads_[i], NULL);
    workers_[i].~Worker();
  }
  ::operator delete(workers_);
  delete[] worker_threads_;
  pthread_barrier_destroy(&thread_barrier_);
}

void ActorScheduler::Enqueue(std::coroutine_handle<> handle) {
  // 本地工作线程任务队列已满,将其推入全局任务队列中
  if (!tls_worker || !(tls_worker->local_queue.push(handle))) {
    global_queue_.push(handle);
  }
}

// 本地调度线程入口函数
void *ActorScheduler::scheduler_thread(void *arg) {
  tls_worker = static_cast<Worker *>(arg);
  ActorScheduler *sched = tls_worker->sched;
  std::mt19937 rng(tls_worker->worker_id);
  char thread_name[16];
  int ret = snprintf(thread_name, 16, "sched-worker-%u", tls_worker->worker_id);
  pthread_setname_np(pthread_self(), thread_name);
  // 线程屏障,等待其他线程初始化完毕
  pthread_barrier_wait(&sched->thread_barrier_);

  StealState result;
  uint32_t target_id;
  uint32_t tick = 0;

  while (sched->is_running()) {
    // 获取已就绪的协程句柄,优先从本地队列获取
    std::coroutine_handle<> task{nullptr};
    // 每执行61次就从全局队列中获取任务
    if (++tick % 61 == 0) {
      task = sched->global_queue_.try_pop();
    }
    if (!task) {
      tls_worker->local_queue.pop(task);
    }
    // 如果本地没有,则随机窃取其他工作线程的任务
    if (!task) {
      target_id = rng() % sched->thread_entries_;
      for (uint32_t i = 0; i != sched->thread_entries_; ++i) {
        // 不能窃取自己的任务队列
        if (target_id != tls_worker->worker_id) {
          result = sched->workers_[target_id].local_queue.steal(task);
          if (result == StealState::kSuccess)
            break;
        }
        target_id = (target_id + 1) % sched->thread_entries_;
      }
    }
    // 当前工作线程没有就绪的任务,且窃取其他工作线程中的任务队列失败
    // 则从全局队列中阻塞获取就绪任务
    if (!task) {
      task = sched->global_queue_.wait_and_pop();
    }
    if (task && !task.done()) {
      task.resume();
    }
  }
  // 退出调度线程
  return NULL;
}

} // namespace ac
