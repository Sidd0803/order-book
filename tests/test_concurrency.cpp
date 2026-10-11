// Phase 2 tests. Run these under ThreadSanitizer as well as Debug:
//   cmake -S . -B build/tsan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=thread
//   cmake --build build/tsan && ./build/tsan/ob_tests "[phase2]"

#include "ob/blocking_queue.hpp"
#include "ob/engine.hpp"
#include "ob/locked_book.hpp"
#include "ob/order_gen.hpp"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <unordered_map>
#include <vector>

using namespace ob;

namespace {

// Each producer gets its own id block so ids never collide across threads.
constexpr OrderId kIdBlock = 1'000'000'000;

std::vector<Command> make_stream(std::uint64_t seed, OrderId first_id, std::size_t n) {
    OrderGenerator gen(seed, first_id);
    std::vector<Command> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(gen.next());
    return out;
}

// Conservation check. Every accepted add ends in exactly one of three states:
// still resting, cancelled, or fully filled. We reconstruct "fully filled"
// from the trade log and verify the three buckets sum to adds_accepted. If a
// command were ever lost or applied twice, this identity breaks.
//
// `book` is consumed: we probe each id with cancel() to confirm the resting
// set matches, so don't use it afterwards.
void check_conservation(OrderBook& book, const std::vector<Command>& all_cmds,
                        const std::vector<Trade>& trades, const Stats& stats) {
    std::unordered_map<OrderId, Quantity> remaining;
    std::uint64_t adds = 0;
    for (const Command& c : all_cmds) {
        if (c.kind == Command::Kind::Add) { remaining[c.id] = c.qty; ++adds; }
    }
    REQUIRE(stats.adds_accepted == adds);   // generator never emits rejectable adds
    REQUIRE(stats.adds_rejected == 0);

    std::uint64_t traded_qty = 0;
    for (const Trade& t : trades) {
        REQUIRE(remaining.at(t.taker_id) >= t.qty);
        REQUIRE(remaining.at(t.maker_id) >= t.qty);
        remaining[t.taker_id] -= t.qty;
        remaining[t.maker_id] -= t.qty;
        traded_qty += t.qty;
    }
    REQUIRE(stats.trades == trades.size());
    REQUIRE(stats.traded_qty == traded_qty);

    std::uint64_t fully_filled = 0;
    for (const auto& [id, left] : remaining) if (left == 0) ++fully_filled;

    const std::uint64_t resting = book.order_count();
    INFO("adds=" << adds << " resting=" << resting << " cancelled=" << stats.cancels_ok
                 << " filled=" << fully_filled);
    REQUIRE(adds == resting + stats.cancels_ok + fully_filled);

    // Every id with qty left is either resting now or was cancelled earlier.
    // cancel() tells us which. Count must match exactly.
    std::uint64_t probed_resting = 0;
    for (const auto& [id, left] : remaining) {
        if (left > 0 && book.cancel(id)) ++probed_resting;
    }
    REQUIRE(probed_resting == resting);
    REQUIRE(book.order_count() == 0);
}

}  // namespace

// ---------------------------------------------------------------------------
TEST_CASE("BlockingQueue delivers in FIFO order and drains on close", "[phase2][queue]") {
    BlockingQueue<int> q;
    q.push(1); q.push(2); q.push(3);
    REQUIRE(q.size() == 3);
    q.close();                     // close with items still queued
    REQUIRE(q.pop() == 1);
    REQUIRE(q.pop() == 2);
    REQUIRE(q.pop() == 3);
    REQUIRE_FALSE(q.pop().has_value());   // drained + closed
    REQUIRE_FALSE(q.pop().has_value());   // stays that way
}

TEST_CASE("BlockingQueue pop blocks until a push arrives", "[phase2][queue]") {
    BlockingQueue<int> q;
    std::optional<int> got;
    std::thread consumer([&] { got = q.pop(); });   // blocks: queue is empty
    q.push(42);
    consumer.join();
    REQUIRE(got == 42);
}

TEST_CASE("BlockingQueue wakes a blocked consumer on close", "[phase2][queue]") {
    BlockingQueue<int> q;
    std::optional<int> got = 7;
    std::thread consumer([&] { got = q.pop(); });
    q.close();
    consumer.join();
    REQUIRE_FALSE(got.has_value());
}

// ---------------------------------------------------------------------------
TEST_CASE("Engine with one producer matches the single-threaded book exactly", "[phase2][engine]") {
    const auto cmds = make_stream(/*seed=*/42, /*first_id=*/1, /*n=*/5000);

    // Reference: apply sequentially on the calling thread.
    OrderBook ref;
    Stats ref_stats;
    std::vector<Trade> ref_trades;
    for (const Command& c : cmds) apply(ref, c, ref_stats, ref_trades);

    // Engine: same commands, same order, but applied on the consumer thread.
    Engine engine;
    for (const Command& c : cmds) engine.submit(c);
    engine.stop();

    // One producer + FIFO queue = identical order of application, so every
    // observable must be identical, down to the trade log.
    REQUIRE(engine.book().dump() == ref.dump());
    REQUIRE(engine.stats().commands == ref_stats.commands);
    REQUIRE(engine.stats().trades == ref_stats.trades);
    REQUIRE(engine.stats().traded_qty == ref_stats.traded_qty);
    REQUIRE(engine.stats().cancels_ok == ref_stats.cancels_ok);
    REQUIRE(engine.trades().size() == ref_trades.size());
    for (std::size_t i = 0; i < ref_trades.size(); ++i) {
        CHECK(engine.trades()[i].taker_id == ref_trades[i].taker_id);
        CHECK(engine.trades()[i].maker_id == ref_trades[i].maker_id);
        CHECK(engine.trades()[i].price    == ref_trades[i].price);
        CHECK(engine.trades()[i].qty      == ref_trades[i].qty);
    }
}

TEST_CASE("Engine with N producers loses and duplicates nothing", "[phase2][engine][stress]") {
    constexpr int producers = 4;
    constexpr std::size_t per_producer = 5000;

    // Pre-generate each producer's stream so the test knows exactly what was
    // sent (the producers themselves just replay it). Interleaving across
    // producers is nondeterministic; the conservation identity must hold for
    // every interleaving.
    std::vector<std::vector<Command>> streams;
    for (int p = 0; p < producers; ++p)
        streams.push_back(make_stream(100 + p, (p + 1) * kIdBlock, per_producer));

    Engine engine;
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&engine, &s = streams[p]] {
            for (const Command& c : s) engine.submit(c);
        });
    }
    for (auto& t : threads) t.join();   // all submits done
    engine.stop();                      // consumer drains and exits

    std::vector<Command> all;
    for (const auto& s : streams) all.insert(all.end(), s.begin(), s.end());
    REQUIRE(engine.stats().commands == all.size());

    OrderBook book = engine.book();   // copy: check_conservation mutates it
    check_conservation(book, all, engine.trades(), engine.stats());
}

TEST_CASE("Engine accessors throw before stop", "[phase2][engine]") {
    Engine engine;
    REQUIRE_THROWS_AS(engine.book(), std::logic_error);
    engine.stop();
    REQUIRE_NOTHROW(engine.book());
    engine.stop();   // idempotent
}

// ---------------------------------------------------------------------------
TEST_CASE("LockedBook with N threads calling directly loses nothing", "[phase2][locked][stress]") {
    constexpr int threads_n = 4;
    constexpr std::size_t per_thread = 5000;

    std::vector<std::vector<Command>> streams;
    for (int p = 0; p < threads_n; ++p)
        streams.push_back(make_stream(200 + p, (p + 1) * kIdBlock, per_thread));

    LockedBook book;
    // Each thread records its own trades and cancels; merged after join.
    // No sharing between threads here, so no locking needed for these.
    std::vector<std::vector<Trade>> trades_per(threads_n);
    std::vector<Stats> stats_per(threads_n);

    std::vector<std::thread> threads;
    for (int p = 0; p < threads_n; ++p) {
        threads.emplace_back([&, p] {
            for (const Command& c : streams[p]) {
                Stats& st = stats_per[p];
                ++st.commands;
                if (c.kind == Command::Kind::Add) {
                    auto t = book.add(c.id, c.side, c.price, c.qty);
                    ++st.adds_accepted;   // ids are unique and qty >= 1, so always accepted
                    for (const Trade& tr : t) { ++st.trades; st.traded_qty += tr.qty; }
                    trades_per[p].insert(trades_per[p].end(), t.begin(), t.end());
                } else {
                    if (book.cancel(c.id)) ++st.cancels_ok; else ++st.cancels_missed;
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    std::vector<Command> all;
    std::vector<Trade> trades;
    Stats stats;
    for (int p = 0; p < threads_n; ++p) {
        all.insert(all.end(), streams[p].begin(), streams[p].end());
        trades.insert(trades.end(), trades_per[p].begin(), trades_per[p].end());
        stats.commands       += stats_per[p].commands;
        stats.adds_accepted  += stats_per[p].adds_accepted;
        stats.trades         += stats_per[p].trades;
        stats.traded_qty     += stats_per[p].traded_qty;
        stats.cancels_ok     += stats_per[p].cancels_ok;
        stats.cancels_missed += stats_per[p].cancels_missed;
    }
    REQUIRE(stats.commands == all.size());

    OrderBook inner = book.unsafe_inner();   // all threads joined: safe
    check_conservation(inner, all, trades, stats);
}
