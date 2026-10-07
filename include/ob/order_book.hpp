#pragma once

#include "ob/types.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob {

// Single-instrument limit order book. Phase 1: single-threaded, clarity first.
//
// Data layout (see SPEC.md, Phase 1):
//
//   bids_ : map<Price, list<Order>>  sorted DESCENDING  (begin() == best bid)
//   asks_ : map<Price, list<Order>>  sorted ASCENDING   (begin() == best ask)
//   index_: OrderId -> where that order lives, so cancel() doesn't have to
//           walk every level looking for it.
//
// Why a list per price level?  Orders at the same price are served in arrival
// order (time priority). A list is naturally FIFO: push_back on arrival,
// match from front(). It also has a property we lean on heavily: erasing one
// node never invalidates iterators to OTHER nodes. That is what lets index_
// hold a list iterator safely. (A vector would shift elements and break them.)
//
// Why two different map types?  std::map's comparator is part of its type, so
// a descending map and an ascending map are unrelated types. That is why the
// matching helper below is a template: one body, instantiated twice.
class OrderBook {
public:
    // Adds a limit order. Matches against the opposite side first; any
    // remainder rests in the book. Returns trades produced (possibly empty).
    // Duplicate id or qty == 0 -> rejected: returns empty, book unchanged.
    std::vector<Trade> add(OrderId id, Side side, Price price, Quantity qty);

    // Cancels a resting order. Returns true if found and removed.
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity depth_at(Side side, Price price) const;   // total resting qty at a level
    std::size_t order_count() const;                   // number of resting orders
    std::string dump() const;   // human-readable book state for the CLI/debugging

private:
    using Level = std::list<Order>;
    using Bids  = std::map<Price, Level, std::greater<Price>>;  // best (highest) first
    using Asks  = std::map<Price, Level, std::less<Price>>;     // best (lowest) first

    // Everything cancel() needs to find and remove an order in O(1):
    // which side, which price level (map lookup is O(log levels), fine for
    // now), and the exact list node.
    struct IndexEntry {
        Side            side;
        Price           price;
        Level::iterator it;
    };

    // ---- helpers you will implement (declared here, defined in order_book.cpp)

    // Walk the opposite side from best price outward while `taker` still
    // crosses and has qty left. Appends to `trades`, removes filled makers
    // (and their index entries, and any emptied levels). `OppositeSide` is
    // Bids or Asks. Note: a member template used only inside order_book.cpp
    // may be defined there; it does not need to live in the header.
    template <class OppositeSide>
    void match(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades);

    // Place the (remaining) taker at its price level and record it in index_.
    void rest(const Order& order);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, IndexEntry> index_;
    std::uint64_t next_seq_ = 0;
};

}  // namespace ob
