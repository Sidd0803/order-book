#include "ob/order_gen.hpp"

#include <algorithm>
#include <cmath>

namespace ob {

OrderGenerator::OrderGenerator(std::uint64_t seed, OrderId first_id, GenConfig cfg)
    : rng_(seed), next_id_(first_id), cfg_(cfg) {}

Command OrderGenerator::next() {
    // Cancel some previously issued order? Only if we have candidates. Picking
    // a random live id and swap-removing it keeps this O(1). The id may already
    // have been filled by the time the cancel is applied; that's realistic and
    // the book simply reports "not found".
    std::uniform_int_distribution<int> pct(0, 99);
    if (!live_.empty() && pct(rng_) < cfg_.cancel_pct) {
        std::uniform_int_distribution<std::size_t> pick(0, live_.size() - 1);
        const std::size_t i = pick(rng_);
        const OrderId id = live_[i];
        live_[i] = live_.back();
        live_.pop_back();
        return Command::cancel(id);
    }

    // Price: mid plus a normally distributed offset. About half of all orders
    // will cross the spread, so the book stays busy on both sides. Clamp to
    // >= 1 so a wild draw never produces a non-positive price.
    std::normal_distribution<double> offset(0.0, cfg_.sigma);
    const Price price = std::max<Price>(1, cfg_.mid + static_cast<Price>(std::lround(offset(rng_))));

    std::uniform_int_distribution<Quantity> qty(1, cfg_.max_qty);
    const Side side = (rng_() & 1) ? Side::Buy : Side::Sell;

    const OrderId id = next_id_++;
    live_.push_back(id);
    return Command::add(id, side, price, qty(rng_));
}

}  // namespace ob
