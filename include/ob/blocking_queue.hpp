#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace ob {

// Thread-safe FIFO for handing work from producer threads to a consumer.
//
// The two primitives and what each one is for:
//
//   std::mutex              Only one thread touches `items_` at a time. Without
//                           it, two pushes could interleave inside deque's
//                           internals and corrupt it. This is the data race.
//
//   std::condition_variable Lets the consumer SLEEP while the queue is empty
//                           instead of spinning on the lock, and lets a
//                           producer wake it. A mutex alone can't express
//                           "wait until there is something to take".
//
// Why both: the consumer must check "is it empty?" and go to sleep as one
// atomic step. If it checked, found empty, released the lock, and THEN slept,
// a push could land in that gap and its notify would be lost: the consumer
// sleeps forever. cv.wait(lock) releases the mutex and sleeps atomically,
// then re-acquires it before returning. That atomicity is the whole point.
//
// Capacity is unbounded (a slow consumer lets memory grow). Phase 2c would
// bound it and make push() block too; that's the classic bounded buffer.
template <class T>
class BlockingQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            items_.push_back(std::move(item));
        }
        // Notify AFTER releasing the lock. Not required for correctness, but if
        // we notified while holding it, the woken consumer would immediately
        // block again trying to acquire a mutex we still own.
        cv_.notify_one();
    }

    // Blocks until an item is available, or the queue is closed and drained.
    // Returns nullopt only in the latter case, so a consumer loop is just:
    //     while (auto cmd = queue.pop()) handle(*cmd);
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mu_);
        // wait(lock, pred) is exactly:  while (!pred()) wait(lock);
        // The loop matters. wait() may return without any notify (a "spurious
        // wakeup", allowed by the standard), and with several consumers another
        // one may have grabbed the item between our wake-up and our re-lock.
        // Re-checking the predicate makes both cases harmless.
        cv_.wait(lock, [this] { return !items_.empty() || closed_; });
        if (items_.empty()) return std::nullopt;   // closed and nothing left
        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    // No more items will be pushed. Items already queued are still delivered,
    // so the consumer drains the queue and then sees nullopt. notify_all
    // because every waiting consumer needs to learn this, not just one.
    void close() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return items_.size();
    }

private:
    // `mutable` because size() is const from the caller's point of view but
    // still has to lock. Locking a mutex mutates it; the logical state of the
    // queue does not change. This is the standard idiom.
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<T> items_;
    bool closed_ = false;   // guarded by mu_, like items_
};

}  // namespace ob
