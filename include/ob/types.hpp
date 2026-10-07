#pragma once

#include <cstdint>

namespace ob {

// Strong-ish aliases. Using distinct names (not raw int64_t everywhere) makes
// signatures self-documenting and makes it easy to change widths later.
using OrderId  = std::uint64_t;
using Price    = std::int64_t;   // integer ticks, never floating point
using Quantity = std::uint32_t;

enum class Side { Buy, Sell };

inline Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

inline const char* to_string(Side s) noexcept {
    return s == Side::Buy ? "B" : "S";
}

// A resting order. Plain aggregate: trivially copyable, no invariants of its
// own. The book owns the invariants (sorted levels, FIFO within a level).
struct Order {
    OrderId       id;
    Side          side;
    Price         price;
    Quantity      qty;   // remaining quantity (decremented as it fills)
    std::uint64_t seq;   // arrival sequence number, for time priority
};

// One fill. The maker is the order that was already resting; the taker is the
// incoming order that crossed. Price is always the maker's resting price.
struct Trade {
    OrderId  taker_id;
    OrderId  maker_id;
    Price    price;
    Quantity qty;
};

}  // namespace ob
