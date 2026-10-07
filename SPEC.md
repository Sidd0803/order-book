# SPEC.md: Order Book Learning Project

## Goal
Build one C++ codebase that grows through three phases (correctness, concurrency, performance) so I can explain, at a conceptual level, how C++ memory, concurrency, and latency work in a trading-style system. Simplicity first; each phase leaves headroom for the next. No GUI: the interface is a CLI plus tests.

## Scope and non-goals
- One instrument, limit orders only, integer prices (ticks), integer quantities.
- Not in scope: market orders, networking, persistence, multiple instruments, a UI.

## Core types (`include/ob/types.hpp`)
```cpp
using OrderId  = std::uint64_t;
using Price    = std::int64_t;   // ticks
using Quantity = std::uint32_t;
enum class Side { Buy, Sell };

struct Order {
    OrderId  id;
    Side     side;
    Price    price;
    Quantity qty;        // remaining quantity
    std::uint64_t seq;   // arrival sequence, for time priority
};

struct Trade {
    OrderId  taker_id;
    OrderId  maker_id;
    Price    price;      // maker's resting price
    Quantity qty;
};
```

## Public API (`include/ob/order_book.hpp`). Keep stable across phases
```cpp
class OrderBook {
public:
    // Adds a limit order. Matches against the opposite side first; any
    // remainder rests in the book. Returns trades produced (possibly empty).
    // Duplicate id -> rejected (return empty, book unchanged).
    std::vector<Trade> add(OrderId id, Side side, Price price, Quantity qty);

    // Cancels a resting order. Returns true if found and removed.
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity depth_at(Side side, Price price) const;
    std::size_t order_count() const;
    std::string dump() const;   // human-readable book state for the CLI/debugging
};
```

## Matching rules
- Price-time priority: best price first; within a price, earliest arrival first.
- A buy crosses if `price >= best_ask`; a sell crosses if `price <= best_bid`.
- Trades execute at the resting (maker) order's price.
- Partial fills are allowed; the unfilled remainder of the taker rests at its limit price.
- Orders with qty == 0 are rejected.

---

## Phase 1: Core book (single-threaded)

**Design:** `std::map<Price, std::list<Order>>` per side (bids descending, asks ascending) plus an `unordered_map<OrderId, iterator-info>` index so `cancel` is O(1) lookup plus O(1) list erase. Start naive; clarity over speed.

**CLI (`src/main.cpp`):** reads commands from stdin or a file, prints trades and book state after each command:
```
ADD <id> <B|S> <price> <qty>
CANCEL <id>
PRINT
```

**Tests (Catch2), at minimum:**
- add to empty book; best_bid/best_ask update
- exact cross; partial fill; sweep across multiple price levels
- time priority within a level
- cancel resting order; cancel unknown id; cancel after fill
- duplicate id and zero qty rejected
- invariant check helper: book is never crossed after any operation

**LLDB checkpoints (required):**
1. Break inside the matching loop; step one order at a time through a multi-level sweep and watch the book change. Use `frame variable`, `p`, and `watchpoint set variable`.
2. Introduce a deliberate bug (e.g., wrong iterator after erase), reproduce it, and find it with the debugger (and ASan) before looking at the code. Write what you found in `docs/debugging-log.md`.

**Done when:**
- [ ] All tests pass under Debug and ASan/UBSan builds
- [ ] CLI demonstrates a sweep, a partial fill, and a cancel
- [ ] Both LLDB checkpoints done and logged
- [ ] I can explain why list-of-orders-per-level gives time priority, and why `cancel` needs the index

---

## Phase 2: Concurrency

**Design:** Producer/consumer. One or more producer threads generate simulated orders (seeded RNG for reproducibility) and push into a queue; one consumer thread applies them to the `OrderBook`. Start with the simplest correct design, then iterate:
1. **2a: Single consumer, mutex + condition_variable queue.** The book itself is touched only by the consumer, so it needs no lock. Understand *why* this is correct.
2. **2b: Multiple threads call the book directly,** protected by one coarse `std::mutex`. Demonstrate the race first (remove the lock, run under TSan), then fix it.
3. **2c (stretch):** finer-grained locking or a lock-free SPSC ring buffer for the queue (`std::atomic`, acquire/release ordering). Only if 2a and 2b are solid.

Wrap concurrency *around* `OrderBook` (e.g., `ConcurrentBook` or `Engine`); do not change `OrderBook`'s API.

**Tests:**
- Deterministic test: same seeded order stream through the single-threaded and concurrent paths yields the same final book (in the 2a design).
- Stress test with N producers; assert no lost or duplicated orders (total in == resting + filled + cancelled).
- TSan run clean on 2a and 2b.

**LLDB checkpoints (required):**
1. Break right before a thread mutates shared state; use `thread list`, `thread select N`, and `bt all` to see both threads side by side.
2. With the lock deliberately removed, catch an inconsistent state in the debugger, then confirm TSan reports the same race. Log both in `docs/debugging-log.md`.

**Done when:**
- [ ] 2a and 2b implemented and tested; TSan clean
- [ ] Race demonstrated, then fixed, and logged
- [ ] Both LLDB checkpoints done and logged
- [ ] I can explain: what a data race is, why a mutex fixes it, what a condition variable is for, and the tradeoff between coarse and fine locking

---

## Phase 3: Performance and memory

**Method:** measure first, change one thing at a time, keep every result in `docs/perf-log.md`. Release builds only. Report relative speedups, not absolute numbers.

**Benchmark harness (`bench/`):** replays a fixed, seeded stream of N orders (mixed add/cancel, realistic skew near the touch) and reports total time and per-op latency percentiles (p50/p99).

**Candidate experiments (do in this order, keep what helps):**
1. Baseline: current map + list design. Record it.
2. Reduce allocations: pool or arena allocator for `Order` nodes, or pre-allocated storage reused across orders.
3. Cache locality: replace `std::list` per level with a contiguous structure (e.g., `std::vector`/`deque` with tombstones or a flat array of levels near the touch).
4. Replace `std::map` of price levels with a flat/array-indexed structure over a bounded tick range.
5. Reduce lock contention or queue overhead from phase 2 (if applicable).
6. Optional: look at `perf`-style hotspots via Instruments (macOS Time Profiler) to confirm where time actually goes.

**LLDB / tooling checkpoints (required):**
1. Use a debugger or Instruments to confirm the hotspot *before* optimizing it (measure, don't guess).
2. Verify an optimization preserved behavior: the Phase 1 test suite must still pass unchanged.

**Done when:**
- [ ] Benchmark harness exists and results are reproducible run to run
- [ ] At least three experiments logged with before/after numbers
- [ ] All earlier tests still pass
- [ ] I can explain: cache lines and locality, why node-based containers are slow for hot paths, allocation cost, and why relative comparisons matter on a noisy laptop

---

## LLDB primer (keep handy)
```
lldb build/debug/ob_cli
(lldb) breakpoint set --file order_book.cpp --line 42   # or: b order_book.cpp:42
(lldb) breakpoint set --name OrderBook::add             # break on a function
(lldb) run < examples/sweep.txt
(lldb) n            # next (step over)        (lldb) s   # step into
(lldb) finish       # run until current function returns
(lldb) c            # continue
(lldb) bt           # backtrace               (lldb) up / down   # move through frames
(lldb) frame variable          # all locals   (lldb) p expr   # print an expression
(lldb) watchpoint set variable best_bid_     # break when a variable changes
(lldb) breakpoint modify -c 'id == 7' 1      # make breakpoint 1 conditional
(lldb) thread list / thread select 2 / bt all   # concurrency (phase 2)
```
Build with `-O0 -g` (the Debug build) or variables will be optimized away.

## Suggested order of work
1. CMake skeleton, Catch2, one failing test, CI of your own: `ctest` green.
2. Phase 1 per the checklist. Don't start phase 2 until it is done.
3. Phase 2, then phase 3. Keep `docs/debugging-log.md` and `docs/perf-log.md` updated as you go; they double as interview talking points.
