#pragma once

#include "ob/types.hpp"

namespace ob {

// One unit of work for the book. Phase 2 moves these between threads, so it
// is a small, trivially copyable value type: no pointers, no ownership, safe
// to copy into a queue and out again.
struct Command {
    enum class Kind { Add, Cancel };

    Kind     kind;
    OrderId  id;
    Side     side  = Side::Buy;   // Add only
    Price    price = 0;           // Add only
    Quantity qty   = 0;           // Add only

    static Command add(OrderId id, Side side, Price price, Quantity qty) {
        return Command{Kind::Add, id, side, price, qty};
    }
    static Command cancel(OrderId id) {
        return Command{Kind::Cancel, id};
    }
};

}  // namespace ob
