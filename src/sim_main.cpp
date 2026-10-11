// Phase 2 driver. Pushes a seeded command stream through one of three paths
// and prints stats plus a fingerprint of the final book, so runs can be
// compared by eye:
//
//   ob_sim --mode sequential --orders 100000 --seed 1
//   ob_sim --mode engine     --orders 100000 --seed 1 --producers 1   # same fingerprint as sequential
//   ob_sim --mode engine     --orders 100000 --seed 1 --producers 4   # fingerprint varies run to run
//   ob_sim --mode locked     --orders 100000 --seed 1 --producers 4
//
// This is also the binary to use for the Phase 2 LLDB checkpoints: it has
// real producer threads and a real consumer to look at with `thread list`.

#include "ob/engine.hpp"
#include "ob/locked_book.hpp"
#include "ob/order_gen.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Args {
    std::string mode = "engine";
    int producers = 2;
    std::size_t orders = 100000;   // total across all producers
    std::uint64_t seed = 1;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if      (k == "--mode")      a.mode = val();
        else if (k == "--producers") a.producers = std::atoi(val().c_str());
        else if (k == "--orders")    a.orders = std::strtoull(val().c_str(), nullptr, 10);
        else if (k == "--seed")      a.seed = std::strtoull(val().c_str(), nullptr, 10);
        else {
            std::cerr << "usage: ob_sim [--mode sequential|engine|locked] [--producers N] [--orders N] [--seed N]\n";
            std::exit(2);
        }
    }
    if (a.producers < 1) a.producers = 1;
    return a;
}

// FNV-1a over dump(): two books with the same fingerprint have the same
// printed state. Cheap way to compare runs without diffing output.
std::uint64_t fingerprint(const std::string& s) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

void report(const char* label, const ob::Stats& st, const ob::OrderBook& book, double ms) {
    std::cout << label << ": " << st.commands << " commands in " << ms << " ms\n"
              << "  adds=" << st.adds_accepted << " (rejected " << st.adds_rejected << ")"
              << "  cancels ok=" << st.cancels_ok << " missed=" << st.cancels_missed
              << "  trades=" << st.trades << " qty=" << st.traded_qty << '\n'
              << "  resting=" << book.order_count()
              << "  bid=" << (book.best_bid() ? std::to_string(*book.best_bid()) : "-")
              << "  ask=" << (book.best_ask() ? std::to_string(*book.best_ask()) : "-")
              << "  fingerprint=" << std::hex << fingerprint(book.dump()) << std::dec << '\n';
}

constexpr ob::OrderId kIdBlock = 1'000'000'000;

}  // namespace

int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    const std::size_t per_producer = a.orders / static_cast<std::size_t>(a.producers);
    using clock = std::chrono::steady_clock;

    if (a.mode == "sequential") {
        ob::OrderGenerator gen(a.seed, kIdBlock);
        ob::OrderBook book;
        ob::Stats st;
        std::vector<ob::Trade> trades;
        const auto t0 = clock::now();
        for (std::size_t i = 0; i < a.orders; ++i) ob::apply(book, gen.next(), st, trades);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        report("sequential", st, book, ms);
        return 0;
    }

    if (a.mode == "engine") {
        ob::Engine engine;
        const auto t0 = clock::now();
        std::vector<std::thread> producers;
        for (int p = 0; p < a.producers; ++p) {
            producers.emplace_back([&, p] {
                // With one producer this is the same stream as "sequential".
                ob::OrderGenerator gen(a.seed + static_cast<std::uint64_t>(p), (p + 1) * kIdBlock);
                for (std::size_t i = 0; i < per_producer; ++i) engine.submit(gen.next());
            });
        }
        for (auto& t : producers) t.join();
        engine.stop();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        report("engine", engine.stats(), engine.book(), ms);
        return 0;
    }

    if (a.mode == "locked") {
        ob::LockedBook book;
        std::vector<ob::Stats> stats(a.producers);
        const auto t0 = clock::now();
        std::vector<std::thread> threads;
        for (int p = 0; p < a.producers; ++p) {
            threads.emplace_back([&, p] {
                ob::OrderGenerator gen(a.seed + static_cast<std::uint64_t>(p), (p + 1) * kIdBlock);
                ob::Stats& st = stats[p];
                for (std::size_t i = 0; i < per_producer; ++i) {
                    const ob::Command c = gen.next();
                    ++st.commands;
                    if (c.kind == ob::Command::Kind::Add) {
                        auto trades = book.add(c.id, c.side, c.price, c.qty);
                        ++st.adds_accepted;
                        for (const auto& t : trades) { ++st.trades; st.traded_qty += t.qty; }
                    } else {
                        if (book.cancel(c.id)) ++st.cancels_ok; else ++st.cancels_missed;
                    }
                }
            });
        }
        for (auto& t : threads) t.join();
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
        ob::Stats total;
        for (const auto& s : stats) {
            total.commands += s.commands; total.adds_accepted += s.adds_accepted;
            total.cancels_ok += s.cancels_ok; total.cancels_missed += s.cancels_missed;
            total.trades += s.trades; total.traded_qty += s.traded_qty;
        }
        report("locked", total, book.unsafe_inner(), ms);
        return 0;
    }

    std::cerr << "unknown mode: " << a.mode << '\n';
    return 2;
}
