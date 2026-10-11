#include "ob/order_book.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>

namespace ob {

// ============================================================================
// Core logic
// ============================================================================

std::vector<Trade> OrderBook::add(OrderId id, Side side, Price price, Quantity qty) {
    std::vector<Trade> trades;

    // Validation first, before we touch any state. A rejected order must leave
    // the book exactly as it was, including next_seq_: rejected orders never
    // "arrived" as far as time priority is concerned.
    if (qty == 0) return trades;
    if (index_.contains(id)) return trades;   // duplicate id

    Order taker{id, side, price, qty, next_seq_++};

    // Match against the OPPOSITE side. A buy consumes asks, a sell consumes
    // bids. The two maps have different types (different comparators), which
    // is why match() is a template rather than taking a single map type.
    if (side == Side::Buy) {
        match(taker, asks_, trades);
    } else {
        match(taker, bids_, trades);
    }

    // Whatever the taker could not fill rests at its own limit price. It is
    // now a maker for future orders. If it filled completely there is nothing
    // to rest and the book never learns this id existed.
    if (taker.qty > 0) rest(taker);

    return trades;
}

bool OrderBook::cancel(OrderId id) {
    auto idx = index_.find(id);
    if (idx == index_.end()) return false;

    // Copy the entry out BEFORE erasing from index_. `idx->second` is a
    // reference into the hash map; once we erase that node the reference
    // dangles. Taking a copy makes the order of the remaining steps irrelevant.
    const IndexEntry entry = idx->second;
    index_.erase(idx);

    // The index told us which side and price level; the map lookup is
    // O(log levels). The list erase itself is O(1) because we stored the
    // exact node iterator. No scanning.
    if (entry.side == Side::Buy) {
        auto level_it = bids_.find(entry.price);
        level_it->second.erase(entry.it);
        if (level_it->second.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(entry.price);
        level_it->second.erase(entry.it);
        if (level_it->second.empty()) asks_.erase(level_it);
    }
    // Removing the empty level matters: best_bid()/best_ask() read
    // map.begin(), so a leftover empty level would report a phantom price.
    return true;
}

template <class OppositeSide>
void OrderBook::match(Order& taker, OppositeSide& opposite, std::vector<Trade>& trades) {
    // Outer loop: one iteration per price level, best price first. Because
    // the map is sorted with the best price at begin(), "best first" is just
    // "walk from begin()". The loop re-reads begin() every time rather than
    // holding an iterator across the erase below.
    while (taker.qty > 0 && !opposite.empty()) {
        auto level_it = opposite.begin();
        const Price level_price = level_it->first;

        // Does the taker's limit reach this level? A buyer pays up to its
        // limit, so it crosses asks priced at or below it. A seller accepts
        // down to its limit, so it crosses bids priced at or above it.
        const bool crosses = (taker.side == Side::Buy) ? taker.price >= level_price
                                                       : taker.price <= level_price;
        if (!crosses) break;   // levels beyond this one are even worse, stop

        // Inner loop: within the level, oldest order first. front() is the
        // earliest arrival because rest() appends with push_back. This is the
        // whole of "time priority": the list order IS the priority order.
        Level& level = level_it->second;
        while (taker.qty > 0 && !level.empty()) {
            Order& maker = level.front();
            const Quantity fill = std::min(taker.qty, maker.qty);

            // Price is the maker's: it was there first and set the terms.
            trades.push_back(Trade{taker.id, maker.id, maker.price, fill});
            taker.qty -= fill;
            maker.qty -= fill;

            if (maker.qty == 0) {
                // Fully filled maker leaves the book. Erase from the index
                // first while `maker` is still a valid reference, then pop.
                index_.erase(maker.id);
                level.pop_front();
            }
            // A partially filled maker stays at the front with reduced qty,
            // keeping its place in line.
        }

        // Emptied the whole level: remove it so the next iteration's begin()
        // is the next-best price. After this erase, level_it and level are
        // both invalid, which is why nothing below touches them.
        if (level.empty()) opposite.erase(level_it);
    }
}

void OrderBook::rest(const Order& order) {
    // operator[] creates the level if it doesn't exist yet (an empty list),
    // then we append. std::prev(end()) is the node we just added; that
    // iterator stays valid for the life of the node no matter what else is
    // inserted or erased around it, which is what makes the index safe.
    Level& level = (order.side == Side::Buy) ? bids_[order.price] : asks_[order.price];
    level.push_back(order);
    index_[order.id] = IndexEntry{order.side, order.price, std::prev(level.end())};
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
