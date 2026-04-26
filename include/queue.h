// Copyright (c) 2026 juantgd. All Rights Reserved.

#ifndef AC_INCLUDE_SPSC_QUEUE_H_
#define AC_INCLUDE_SPSC_QUEUE_H_

#include <atomic>
#include <bit>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
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
  struct Slot {
    std::atomic<std::size_t> sequence;
    EventMessage *message;
  };

  MailQueue(std::size_t size) {
    size_ = std::bit_ceil(size);
    mask_ = size_ - 1;
    queue_ = new Slot[size_];
    for (std::size_t i = 0; i != size_; ++i) {
      queue_[i].sequence.store(i, std::memory_order_relaxed);
      queue_[i].message = nullptr;
    }
  }
  ~MailQueue() {
    if (queue_) {
      delete[] queue_;
      queue_ = nullptr;
    }
  }
  bool try_enqueue(EventMessage *message) {
    Slot *slot;
    std::size_t write_idx = write_index_.load(std::memory_order_relaxed);
    while (true) {
      slot = &queue_[write_idx & mask_];
      std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
      auto diff = static_cast<std::ptrdiff_t>(sequence) -
                  static_cast<std::ptrdiff_t>(write_idx);
      // 当前槽位尚未被消费者释放,队列已满
      if (diff < 0) {
        return false;
      }
      if (diff == 0 &&
          write_index_.compare_exchange_weak(write_idx, write_idx + 1,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
        break;
      }
      write_idx = write_index_.load(std::memory_order_relaxed);
    }
    slot->message = message;
    slot->sequence.store(write_idx + 1, std::memory_order_release);
    return true;
  }
  bool try_dequeue(EventMessage **message) {
    std::size_t read_idx = read_index_.load(std::memory_order_relaxed);
    Slot *slot = &queue_[read_idx & mask_];
    std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
    auto diff = static_cast<std::ptrdiff_t>(sequence) -
                static_cast<std::ptrdiff_t>(read_idx + 1);
    // 当前槽位尚未发布完成,队列为空
    if (diff < 0) {
      return false;
    }
    *message = slot->message;
    slot->message = nullptr;
    read_index_.store(read_idx + 1, std::memory_order_relaxed);
    slot->sequence.store(read_idx + size_, std::memory_order_release);
    return true;
  }

  bool can_dequeue() const {
    std::size_t read_idx = read_index_.load(std::memory_order_relaxed);
    const Slot *slot = &queue_[read_idx & mask_];
    std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
    auto diff = static_cast<std::ptrdiff_t>(sequence) -
                static_cast<std::ptrdiff_t>(read_idx + 1);
    return diff >= 0;
  }

private:
  alignas(kCacheLineSize) std::atomic<std::size_t> write_index_{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> read_index_{0};
  alignas(kCacheLineSize) Slot *queue_{nullptr};
  std::size_t size_;
  std::size_t mask_;
};

template <std::size_t N> class WorkStealingQueue {
  static_assert(std::has_single_bit(N), "queue size must be a power of 2.");
  static constexpr std::size_t kMask = N - 1;

public:
  WorkStealingQueue() {
    for (auto &slot : queue_) {
      slot.store(nullptr, std::memory_order_relaxed);
    }
  }
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
      handle = std::coroutine_handle<>::from_address(ptr);
      return ptr ? StealState::kSuccess : StealState::kEmpty;
    }
    // 当前队列为空,恢复bottom
    bottom_.store(bt + 1, std::memory_order_relaxed);
    handle = std::coroutine_handle<>::from_address(nullptr);
    return StealState::kEmpty;
  }

  StealState steal(std::coroutine_handle<> &handle) {
    std::size_t tp = top_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    std::size_t bt = bottom_.load(std::memory_order_acquire);
    // 与pop保持一致,当owner在空队列上预减bottom时,这里需要按有符号值判断
    if (static_cast<std::int64_t>(tp) < static_cast<std::int64_t>(bt)) {
      void *ptr = queue_[tp & kMask].load(std::memory_order_relaxed);
      if (!top_.compare_exchange_strong(tp, tp + 1, std::memory_order_seq_cst,
                                        std::memory_order_relaxed)) {
        // 竞争失败,其他线程已经窃取该元素
        handle = std::coroutine_handle<>::from_address(nullptr);
        return StealState::kAbort;
      }
      // 竞争成功
      handle = std::coroutine_handle<>::from_address(ptr);
      return StealState::kSuccess;
    }
    // 队列为空
    handle = std::coroutine_handle<>::from_address(nullptr);
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

  void push(const std::coroutine_handle<> &task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back(task.address());
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
      for (const auto &task : tasks)
        queue_.push_back(task.address());
    }
    std::size_t idle_nr = idle_threads_.load(std::memory_order_relaxed);
    std::size_t wake_count = std::min(tasks.size(), idle_nr);
    LOG_D("idle_nr: {},tasks: {}", idle_nr, tasks.size());
    // 精确唤醒,避免多个工作线程竞争同一个任务
    while (wake_count--) {
      cond_.notify_one();
    }
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
      task = std::coroutine_handle<>::from_address(queue_.front());
      queue_.pop_front();
    }
    return task;
  }

  // 尝试获取任务,用于工作线程轮询获取就绪任务
  std::coroutine_handle<> try_pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty())
      return nullptr;
    auto task = std::coroutine_handle<>::from_address(queue_.front());
    queue_.pop_front();
    return task;
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_flag)
        return;
      stop_flag = true;
    }
    cond_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable cond_;
  std::deque<void *> queue_;
  std::atomic<std::size_t> idle_threads_{0};
  bool stop_flag{false};
};

// extern template class SPSCQueue<EventMessage *, kMailBoxSize>;
extern template class WorkStealingQueue<kWorkStealingQueueSize>;

} // namespace ac

#endif
