#ifndef AC_INCLUDE_SPSC_QUEUE_H_
#define AC_INCLUDE_SPSC_QUEUE_H_

#include <atomic>
#include <bit>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

#include "helper.h"

namespace ac {

namespace {

inline static constexpr std::size_t kMailBoxSize = 1U << 8;

inline static constexpr std::size_t kWorkStealingQueueSize = 1U << 8;

} // namespace

struct EventMessage;

enum class StealState { kEmpty = 0, kSuccess, kAbort };

template <typename T, std::size_t N> class SPSCQueue {
  static_assert(std::has_single_bit(N), "queue size must be a power of 2.");
  static constexpr std::size_t kMask = N - 1;

public:
  bool try_enqueue(T &item) {
    std::size_t tail = write_index_.load(std::memory_order_relaxed);
    // std::size_t next_tail = (tail + 1) & kMask;
    std::size_t front = read_index_.load(std::memory_order_acquire);
    // 队列元素已满
    if (((tail + 1) & kMask) == (front & kMask))
      return false;
    queue_[tail & kMask] = item;
    write_index_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool try_dequeue(T &item) {
    std::size_t front = read_index_.load(std::memory_order_relaxed);
    std::size_t tail = write_index_.load(std::memory_order_acquire);
    // 队列为空
    if (front == tail)
      return false;
    item = queue_[front & kMask];
    read_index_.store(front + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(kCacheLineSize) std::atomic<std::size_t> write_index_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> read_index_{0};
  alignas(kCacheLineSize) T queue_[N];
};

// MPSC队列
class MailQueue {
public:
  MailQueue(std::size_t size) {
    size_ = std::bit_ceil(size);
    queue_ = new std::atomic<void *>[size_];
  }
  ~MailQueue() {
    if (queue_) {
      delete queue_;
      queue_ = nullptr;
    }
  }
  bool try_enqueue(EventMessage *message) {
    std::size_t read_idx;
    std::size_t next_write_idx;
    std::size_t write_idx = write_index_.load(std::memory_order_relaxed);
    do {
      read_idx = read_index_.load(std::memory_order_acquire);
      // 队列元素已满
      if (write_idx - read_idx >= size_)
        return false;
      next_write_idx = write_idx + 1;
    } while (!write_index_.compare_exchange_weak(write_idx, next_write_idx,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed));
    // CAS抢占成功
    queue_[write_idx & (size_ - 1)].store(message, std::memory_order_relaxed);
    return true;
  }
  bool try_dequeue(EventMessage **message) {
    std::size_t read_idx = read_index_.load(std::memory_order_relaxed);
    std::size_t write_idx = write_index_.load(std::memory_order_acquire);
    // 队列为空
    if (read_idx == write_idx)
      return false;
    *message = static_cast<EventMessage *>(
        queue_[read_idx & (size_ - 1)].load(std::memory_order_relaxed));
    read_index_.store(read_idx + 1, std::memory_order_release);
    return true;
  }

private:
  alignas(kCacheLineSize) std::atomic<std::size_t> write_index_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> read_index_{0};
  alignas(kCacheLineSize) std::atomic<void *> *queue_;
  std::size_t size_;
};

template <std::size_t N> class WorkStealingQueue {
  static_assert(std::has_single_bit(N), "queue size must be a power of 2.");
  static constexpr std::size_t kMask = N - 1;

public:
  WorkStealingQueue() = default;
  ~WorkStealingQueue() = default;

  bool push(const std::coroutine_handle<> &handle) {
    std::size_t bt = bottom_.load(std::memory_order_relaxed);
    std::size_t tp = top_.load(std::memory_order_acquire);
    if (bt - tp >= N)
      return false;
    queue_[bt & kMask].store(handle.address(), std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    bottom_.store(bt + 1, std::memory_order_relaxed);
    return true;
  }

  StealState pop(std::coroutine_handle<> &handle) {
    // 若bottom目前为0,则为bt负数,当bottom大于int64_t的最大值时,bt变为很小的负数
    std::size_t bt = bottom_.load(std::memory_order_relaxed) - 1;
    // 预取最后一个元素
    bottom_.store(bt, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::size_t tp = top_.load(std::memory_order_relaxed);
    // 若bt为负数,则tp >
    // bt,若top和bottom都大于int64_t上限,则tp和bt都会溢出为负数,此时bt依旧>=tp,或当前队列为空
    if (static_cast<std::int64_t>(tp) <= static_cast<std::int64_t>(bt)) {
      void *ptr = queue_[bt & kMask].load(std::memory_order_relaxed);
      // 最后一个元素,pop可能会与steal发生竞争
      if (tp == bt) {
        // 进行CAS操作,当其他线程成功竞争窃取头部元素后,当前bottom的预取操作需要进行恢复
        // 即当前top已经被修改,所以pop与steal竞争最后一个元素失败
        if (!top_.compare_exchange_strong(tp, tp + 1, std::memory_order_seq_cst,
                                          std::memory_order_relaxed)) {
          // 竞争失败,已被其他线程窃取最后一个元素
          ptr = nullptr;
        }
        // 无论竞争成功还是失败,top都已经被修改,所以需要恢复bottom
        bottom_.store(bt + 1, std::memory_order_relaxed);
      }
      handle.from_address(ptr);
      return ptr ? StealState::kSuccess : StealState::kEmpty;
    }
    // 当前队列为空,恢复bottom
    bottom_.store(bt + 1, std::memory_order_relaxed);
    handle.from_address(nullptr);
    return StealState::kEmpty;
  }

  StealState steal(std::coroutine_handle<> &handle) {
    std::size_t tp = top_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::size_t bt = bottom_.load(std::memory_order_acquire);
    // 队列不为空
    if (tp < bt) {
      void *ptr = queue_[tp & kMask].load(std::memory_order_relaxed);
      if (!top_.compare_exchange_strong(tp, tp + 1, std::memory_order_seq_cst,
                                        std::memory_order_relaxed)) {
        // 竞争失败,其他线程已经窃取该元素
        handle.from_address(nullptr);
        return StealState::kAbort;
      }
      // 竞争成功
      handle.from_address(ptr);
      return StealState::kSuccess;
    }
    // 队列为空
    handle.from_address(nullptr);
    return StealState::kEmpty;
  }

private:
  alignas(kCacheLineSize) std::atomic<std::size_t> top_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> bottom_{0};
  alignas(kCacheLineSize) std::atomic<void *> queue_[N];
};

class GlobalQueue {
public:
  GlobalQueue() = default;
  ~GlobalQueue() = default;

  void push(std::coroutine_handle<> task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(task);
    }
    if (idle_threads_.load(std::memory_order_relaxed) > 0) {
      // 仅唤醒一个工作线程执行
      cond_.notify_one();
    }
  }

  void push_batch(const std::vector<std::coroutine_handle<>> &tasks) {
    if (tasks.empty()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.insert(queue_.end(), tasks.begin(), tasks.end());
    }
    std::size_t idle_nr = idle_threads_.load(std::memory_order_relaxed);
    std::size_t wake_count = std::min(tasks.size(), idle_nr);
    // 精确唤醒,避免多个工作线程竞争同一个任务
    do {
      cond_.notify_one();
    } while (--wake_count);
  }

  // 当工作线程中没有任务时，弹出全局队列中的就绪任务
  // 若全局队列中没有就绪任务, 则阻塞工作线程的执行
  std::coroutine_handle<> wait_and_pop() {
    std::coroutine_handle<> task = nullptr;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      idle_threads_.fetch_add(1, std::memory_order_relaxed);
      cond_.wait(lock, [this]() { return !queue_.empty() || stop_flag; });
      idle_threads_.fetch_sub(1, std::memory_order_relaxed);
      if (stop_flag || queue_.empty()) {
        return nullptr;
      }
      // TODO: 当队列中就绪的任务过多时,直接弹出一大批任务,避免频繁的锁竞争
      task = queue_.front();
      queue_.pop_front();
    }
    return task;
  }

  // 尝试获取任务,用于工作线程轮询获取就绪任务
  std::coroutine_handle<> try_pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty())
      return nullptr;
    std::coroutine_handle<> task = queue_.front();
    queue_.pop_front();
    return task;
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_flag = true;
    }
    cond_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::deque<std::coroutine_handle<>> queue_;
  std::atomic<std::size_t> idle_threads_{0};
  bool stop_flag{false};
};

// extern template class SPSCQueue<EventMessage *, kMailBoxSize>;
extern template class WorkStealingQueue<kWorkStealingQueueSize>;

} // namespace ac

#endif
