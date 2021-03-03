#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <utility>

#include <glog/logging.h>

namespace bm {
namespace core {


template<typename Item>
class ThreadsafeQueue {
 public:
  ThreadsafeQueue(size_t max_queue_size,
                  bool drop_oldest_if_full = true)
      : max_queue_size_(max_queue_size),
        drop_oldest_if_full_(drop_oldest_if_full) {}

  // Push an item onto the queue.
  bool Push(Item item)
  {
    bool did_push = false;
    lock_.lock();
    if (q_.size() >= max_queue_size_) {
      if (drop_oldest_if_full_) {
        q_.pop();
        q_.push(item);
        did_push = true;
      }
    } else {
      q_.push(item);
      did_push = true;
    }
    lock_.unlock();
    return did_push;
  }

  // Pop the item at the front of the queue (oldest).
  // NOTE(milo): This could cause problems if there are MULTIPLE things popping from the queue! Only
  // use with a single consumer!
  Item Pop()
  {
    lock_.lock();
    CHECK_GT(q_.size(), 0) << "Tried to pop from ThreadSafeQueue of zero size!" << std::endl;
    const Item item = q_.front();
    q_.pop();
    lock_.unlock();
    return item;
  }

  // Check if the queue is empty and pop the front item if so. This is safe to use with multiple
  // consumers, because we check non-emptiness and pop within the same lock.
  bool PopIfNonEmpty(Item& item)
  {
    lock_.lock();
    const bool nonempty = !q_.empty();
    if (nonempty) {
      item = q_.front();
      q_.pop();
    }
    lock_.unlock();
    return nonempty;
  }

  // Return the current size of the queue.
  size_t Size()
  {
    lock_.lock();
    const size_t N = q_.size();
    lock_.unlock();
    return N;
  }

  bool Empty() { return Size() == 0; }

 private:
  size_t max_queue_size_ = 100;
  bool drop_oldest_if_full_ = true;

  std::queue<Item> q_;
  std::mutex lock_;
};

}
}