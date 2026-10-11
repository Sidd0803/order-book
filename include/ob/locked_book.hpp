#pragma once

#include "ob/order_book.hpp"

#include <mutex>

namespace ob {

// Phase 2b: many threads call the book directly. One coarse mutex serialises
// every call, so from the book's point of view it is still single-threaded.
//
// This is the simplest correct design and also the slowest under contention:
// a cancel on one side of the book blocks an add on the other side even
// though they touch different data. Finer-grained locking (per side, per
// level) would allow more parallelism at the cost of much harder reasoning
// about lock ordering and deadlocks. That tradeoff is the Phase 2 "explain"
// item; Phase 3 measures it.
//
// Why wrap rather than put the mutex inside OrderBook? The single-threaded
// book stays single-threaded and testable on its own, and the Engine (2a)
// can use it with no lock at all. Concurrency is a policy layered on top.
//
// OB_RACE_DEMO: build with -DOB_RACE_DEMO=ON to compile this class WITHOUT
// the lock. Every method still "locks" a NoLock that does nothing, so the
// code shape is identical but the mutex is never taken. Run the stress test
// under ThreadSanitizer and watch it light up. Then turn the option off and
// watch it go quiet. That before/after is Phase 2's second LLDB checkpoint.
class LockedBook {
public:
    std::vector<Trade> add(OrderId id, Side side, Price price, Quantity qty) {
        Lock lock(mu_);
        return book_.add(id, side, price, qty);
    }

    bool cancel(OrderId id) {
        Lock lock(mu_);
        return book_.cancel(id);
    }

    std::optional<Price> best_bid() const { Lock lock(mu_); return book_.best_bid(); }
    std::optional<Price> best_ask() const { Lock lock(mu_); return book_.best_ask(); }
    Quantity depth_at(Side side, Price price) const { Lock lock(mu_); return book_.depth_at(side, price); }
    std::size_t order_count() const { Lock lock(mu_); return book_.order_count(); }
    std::string dump() const { Lock lock(mu_); return book_.dump(); }

    // Escape hatch for tests that want to inspect the wrapped book after all
    // threads have joined. Not safe while other threads are still running.
    const OrderBook& unsafe_inner() const { return book_; }

private:
#ifdef OB_RACE_DEMO
    struct NoLock {
        explicit NoLock(std::mutex&) {}   // takes the mutex, ignores it
    };
    using Lock = NoLock;
#else
    // lock_guard is RAII: the constructor locks, the destructor unlocks, so
    // every return path (including exceptions) releases the mutex. You never
    // write unlock() by hand.
    using Lock = std::lock_guard<std::mutex>;
#endif

    mutable std::mutex mu_;   // mutable: const queries still need to lock
    OrderBook book_;
};

}  // namespace ob
