#include "ob/order_book.hpp"

#include <sstream>

namespace ob {

// ============================================================================
// Core logic: YOURS to write. The queries further down are already done.
// ============================================================================

std::vector<Trade> OrderBook::add(OrderId id, Side side, Price price, Quantity qty) {
    std::vector<Trade> trades;

    // TODO(you) Phase 1, step 1: validation
    //   - qty == 0            -> reject (return empty, book unchanged)
    //   - index_ contains id  -> reject (duplicate)
    //
    // TODO(you) step 2: build the taker Order with seq = next_seq_++.
    //   Think: should seq be consumed for rejected orders? (Either is fine,
    //   but be deliberate. Hint: tests only care that accepted orders are
    //   ordered by arrival.)
    //
    // TODO(you) step 3: match against the opposite side.
    //   if side == Buy  -> match(taker, asks_, trades)
    //   if side == Sell -> match(taker, bids_, trades)
    //
    // TODO(you) step 4: if taker.qty > 0 after matching, rest(taker).
    //
    // Mark unused until implemented (keeps -Wextra quiet):
    (void)id; (void)side; (void)price; (void)qty;

    return trades;
}

bool OrderBook::cancel(OrderId id) {
    // TODO(you):
    //   1. Look up id in index_. Not found -> return false.
    //   2. Use entry.side + entry.price to find the Level in bids_/asks_.
    //   3. level.erase(entry.it)          <- O(1), this is why we store the iterator
    //   4. If the level is now empty, erase the level from the map
    //      (otherwise best_bid()/best_ask() would report a phantom price).
    //   5. Erase id from index_. Return true.
    //
    // Order of operations matters in step 3-5: once you erase the index
    // entry, `entry` (if it was a reference into index_) is dangling. Copy it
    // out first, or erase the index last. <- this is a good deliberate-bug
    // candidate for LLDB checkpoint 2.
    (void)id;
    return false;
}

template <class OppositeSide>
void OrderBook::match(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades) {
    // TODO(you):
    //   while taker.qty > 0 and opposite is not empty:
    //       auto level_it = opposite.begin();          // best price on that side
    //       Price level_price = level_it->first;
    //       crosses? Buy taker crosses if taker.price >= level_price,
    //                Sell taker crosses if taker.price <= level_price.
    //                (Hint: taker.side tells you which rule applies.)
    //       if not crossing -> break
    //
    //       Level& level = level_it->second;
    //       while taker.qty > 0 and !level.empty():
    //           Order& maker = level.front();          // earliest arrival = time priority
    //           Quantity fill = min(taker.qty, maker.qty);
    //           trades.push_back({taker.id, maker.id, maker.price, fill});
    //           taker.qty -= fill; maker.qty -= fill;
    //           if maker.qty == 0: index_.erase(maker.id); level.pop_front();
    //
    //       if level.empty(): opposite.erase(level_it);
    //
    // Iterator hygiene: after opposite.erase(level_it), level_it is dead.
    // The loop re-reads opposite.begin() each iteration so that's fine, as
    // long as you never touch level_it after the erase.
    (void)taker; (void)opposite; (void)trades;
}

void OrderBook::rest(const Order& order) {
    // TODO(you):
    //   Pick the map by order.side. operator[] creates the level if missing.
    //   push_back(order), then record {side, price, iterator-to-the-new-node}
    //   in index_. std::prev(level.end()) is the node you just appended.
    (void)order;
}

// ============================================================================
// Queries: already implemented (boilerplate).
// ============================================================================

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;   // descending map: begin() is the highest price
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;   // ascending map: begin() is the lowest price
}

Quantity OrderBook::depth_at(Side side, Price price) const {
    auto sum = [](const Level& level) {
        Quantity total = 0;
        for (const Order& o : level) total += o.qty;
        return total;
    };
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? 0 : sum(it->second);
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? 0 : sum(it->second);
}

std::size_t OrderBook::order_count() const {
    // Every resting order has exactly one index entry, so this is O(1).
    return index_.size();
}

std::string OrderBook::dump() const {
    // Asks printed highest -> lowest, then bids highest -> lowest, so the
    // "touch" (best ask / best bid) sits in the middle like a real ladder.
    std::ostringstream os;
    auto print_level = [&](Price price, const Level& level) {
        Quantity total = 0;
        for (const Order& o : level) total += o.qty;
        os << "  " << price << " x " << total << "  [";
        bool first = true;
        for (const Order& o : level) {
            os << (first ? "" : ", ") << o.id << ":" << o.qty;
            first = false;
        }
        os << "]\n";
    };

    os << "--- ASKS ---\n";
    for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) print_level(it->first, it->second);
    os << "--- BIDS ---\n";
    for (const auto& [price, level] : bids_) print_level(price, level);
    os << "(" << index_.size() << " resting orders)\n";
    return os.str();
}

}  // namespace ob
