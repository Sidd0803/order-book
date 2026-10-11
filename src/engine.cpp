#include "ob/engine.hpp"

#include <stdexcept>

namespace ob {

void apply(OrderBook& book, const Command& cmd, Stats& stats, std::vector<Trade>& out) {
    ++stats.commands;
    switch (cmd.kind) {
    case Command::Kind::Add: {
        const std::size_t before = book.order_count();
        std::vector<Trade> trades = book.add(cmd.id, cmd.side, cmd.price, cmd.qty);
        // Accepted if it traded or it rested; rejected if neither happened.
        const bool accepted = !trades.empty() || book.order_count() > before;
        if (accepted) ++stats.adds_accepted; else ++stats.adds_rejected;
        for (const Trade& t : trades) {
            ++stats.trades;
            stats.traded_qty += t.qty;
        }
        out.insert(out.end(), trades.begin(), trades.end());
        break;
    }
    case Command::Kind::Cancel:
        if (book.cancel(cmd.id)) ++stats.cancels_ok; else ++stats.cancels_missed;
        break;
    }
}

Engine::Engine() : consumer_([this] { run(); }) {}

Engine::~Engine() { stop(); }

void Engine::submit(Command cmd) { queue_.push(cmd); }

void Engine::stop() {
    if (stopped_) return;
    stopped_ = true;
    queue_.close();      // consumer drains what's left, then pop() returns nullopt
    consumer_.join();    // wait for it. After this line, book_/stats_/trades_ are ours.
}

void Engine::run() {
    // The entire consumer. pop() blocks while the queue is empty, so this
    // thread uses no CPU while idle. It exits only when close() has been
    // called AND the queue is drained.
    while (std::optional<Command> cmd = queue_.pop()) {
        apply(book_, *cmd, stats_, trades_);
    }
}

void Engine::require_stopped(const char* what) const {
    if (!stopped_) throw std::logic_error(std::string("Engine::") + what + "() called before stop()");
}

const OrderBook&          Engine::book()   const { require_stopped("book");   return book_; }
const Stats&              Engine::stats()  const { require_stopped("stats");  return stats_; }
const std::vector<Trade>& Engine::trades() const { require_stopped("trades"); return trades_; }

}  // namespace ob
