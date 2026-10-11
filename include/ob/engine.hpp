#pragma once

#include "ob/blocking_queue.hpp"
#include "ob/command.hpp"
#include "ob/order_book.hpp"

#include <cstdint>
#include <thread>
#include <vector>

namespace ob {

// Counters the consumer keeps as it applies commands. Plain integers, no
// atomics: in the Engine only ONE thread ever writes them, and readers wait
// for that thread to finish (join) before looking.
struct Stats {
    std::uint64_t commands       = 0;
    std::uint64_t adds_accepted  = 0;
    std::uint64_t adds_rejected  = 0;
    std::uint64_t cancels_ok     = 0;
    std::uint64_t cancels_missed = 0;
    std::uint64_t trades         = 0;
    std::uint64_t traded_qty     = 0;
};

// Applies one command to a book, updates stats, appends any trades to `out`.
// Both the single-threaded reference path and the Engine's consumer call this
// same function, which is what lets the tests assert the two paths agree.
void apply(OrderBook& book, const Command& cmd, Stats& stats, std::vector<Trade>& out);

// Phase 2a: producer/consumer with a single consumer that OWNS the book.
//
// Why this needs no lock around OrderBook: the book is a private member and
// the only code that touches it is run(), which executes on exactly one
// thread (consumer_). Producers never see the book; they only see submit(),
// which talks to the queue. All the synchronisation lives inside
// BlockingQueue. The book remains a plain single-threaded object.
//
// Lifecycle: constructor starts the consumer thread, stop() closes the queue
// and joins. After stop() returns, book()/stats()/trades() are safe to read:
// std::thread::join() guarantees everything the consumer did is visible to
// the joining thread (it "happens-before" the return from join).
class Engine {
public:
    Engine();
    ~Engine();   // calls stop() if you forgot; a joinable std::thread being destroyed is std::terminate

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Callable from any number of threads concurrently, until stop().
    void submit(Command cmd);

    // Close the queue, let the consumer drain it, join. Idempotent.
    void stop();

    // Only valid after stop(). Throws std::logic_error otherwise, because
    // reading while the consumer is still writing would be a data race.
    const OrderBook&          book()   const;
    const Stats&              stats()  const;
    const std::vector<Trade>& trades() const;

private:
    void run();   // consumer loop; the only function that touches book_
    void require_stopped(const char* what) const;

    BlockingQueue<Command> queue_;
    OrderBook              book_;
    Stats                  stats_;
    std::vector<Trade>     trades_;
    std::thread            consumer_;
    bool                   stopped_ = false;   // touched only by the thread that owns the Engine
};

}  // namespace ob
