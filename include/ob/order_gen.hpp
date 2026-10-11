#pragma once

#include "ob/command.hpp"

#include <cstdint>
#include <random>
#include <vector>

namespace ob {

// Deterministic stream of simulated commands. Same seed, same stream, every
// run. That determinism is what makes the Phase 2 tests possible: we can push
// an identical stream through the single-threaded book and through the
// concurrent engine and demand identical results.
//
// Each generator owns a disjoint id range starting at `first_id`, so several
// producers can run side by side without ever colliding on an OrderId.
struct GenConfig {
    Price    mid        = 1000;   // prices cluster around here
    double   sigma      = 4.0;    // std-dev of the price offset, in ticks
    Quantity max_qty    = 100;    // qty uniform in [1, max_qty]
    int      cancel_pct = 20;     // chance a command is a cancel (0-100)
};

class OrderGenerator {
public:
    OrderGenerator(std::uint64_t seed, OrderId first_id, GenConfig cfg = {});

    Command next();

    // ids issued so far are exactly [first_id, next_id()).
    OrderId next_id() const { return next_id_; }

private:
    std::mt19937_64 rng_;
    OrderId next_id_;
    GenConfig cfg_;
    std::vector<OrderId> live_;   // ids we've added and not yet tried to cancel
};

}  // namespace ob
