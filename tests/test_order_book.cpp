// Phase 1 tests. Everything here is specified in SPEC.md; the tests fail
// until add()/cancel()/match()/rest() are implemented.

#include "ob/order_book.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ob;

namespace {

// Invariant: the book is never crossed. If both sides exist, best bid must be
// strictly below best ask; otherwise a match should have happened.
void require_not_crossed(const OrderBook& book) {
    auto bid = book.best_bid();
    auto ask = book.best_ask();
    if (bid && ask) {
        INFO("best_bid=" << *bid << " best_ask=" << *ask);
        REQUIRE(*bid < *ask);
    }
}

// Convenience: add and check the invariant in one call.
std::vector<Trade> add_checked(OrderBook& book, OrderId id, Side side, Price price, Quantity qty) {
    auto trades = book.add(id, side, price, qty);
    require_not_crossed(book);
    return trades;
}

}  // namespace

// ---------------------------------------------------------------------------
// This one passes on the skeleton: it only uses the query functions.
TEST_CASE("empty book has no best prices and no orders", "[phase1][skeleton]") {
    OrderBook book;
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE_FALSE(book.best_ask().has_value());
    REQUIRE(book.order_count() == 0);
    REQUIRE(book.depth_at(Side::Buy, 100) == 0);
    REQUIRE(book.depth_at(Side::Sell, 100) == 0);
    require_not_crossed(book);
}

// ---------------------------------------------------------------------------
TEST_CASE("add to empty book rests and updates best prices", "[phase1][add]") {
    OrderBook book;

    auto trades = add_checked(book, 1, Side::Buy, 100, 10);
    REQUIRE(trades.empty());
    REQUIRE(book.best_bid() == 100);
    REQUIRE_FALSE(book.best_ask().has_value());
    REQUIRE(book.depth_at(Side::Buy, 100) == 10);
    REQUIRE(book.order_count() == 1);

    trades = add_checked(book, 2, Side::Sell, 105, 7);
    REQUIRE(trades.empty());
    REQUIRE(book.best_ask() == 105);
    REQUIRE(book.depth_at(Side::Sell, 105) == 7);
    REQUIRE(book.order_count() == 2);
}

TEST_CASE("best prices track the best level, not insertion order", "[phase1][add]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 98, 1);
    add_checked(book, 2, Side::Buy, 100, 1);   // better bid
    add_checked(book, 3, Side::Buy, 99, 1);
    REQUIRE(book.best_bid() == 100);

    add_checked(book, 4, Side::Sell, 104, 1);
    add_checked(book, 5, Side::Sell, 102, 1);  // better ask
    add_checked(book, 6, Side::Sell, 103, 1);
    REQUIRE(book.best_ask() == 102);
}

// ---------------------------------------------------------------------------
TEST_CASE("exact cross fills both orders completely", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 10);

    auto trades = add_checked(book, 2, Side::Buy, 100, 10);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].taker_id == 2);
    CHECK(trades[0].maker_id == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].qty == 10);

    REQUIRE(book.order_count() == 0);
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE_FALSE(book.best_ask().has_value());
}

TEST_CASE("trade executes at the maker's resting price", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 5);
    auto trades = add_checked(book, 2, Side::Buy, 110, 5);   // aggressive taker
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].price == 100);   // not 110
}

TEST_CASE("partial fill: taker smaller than maker leaves maker remainder", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 50);
    auto trades = add_checked(book, 2, Side::Buy, 100, 20);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 20);
    REQUIRE(book.depth_at(Side::Sell, 100) == 30);
    REQUIRE(book.order_count() == 1);
}

TEST_CASE("partial fill: taker larger than maker rests remainder at its limit", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 20);
    auto trades = add_checked(book, 2, Side::Buy, 100, 50);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 20);
    REQUIRE_FALSE(book.best_ask().has_value());
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.depth_at(Side::Buy, 100) == 30);
    REQUIRE(book.order_count() == 1);
}

TEST_CASE("sweep across multiple price levels", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 101, 10);
    add_checked(book, 2, Side::Sell, 102, 10);
    add_checked(book, 3, Side::Sell, 103, 10);
    add_checked(book, 4, Side::Sell, 104, 10);   // above the taker's limit, untouched

    auto trades = add_checked(book, 5, Side::Buy, 103, 25);
    REQUIRE(trades.size() == 3);
    CHECK(trades[0].maker_id == 1); CHECK(trades[0].price == 101); CHECK(trades[0].qty == 10);
    CHECK(trades[1].maker_id == 2); CHECK(trades[1].price == 102); CHECK(trades[1].qty == 10);
    CHECK(trades[2].maker_id == 3); CHECK(trades[2].price == 103); CHECK(trades[2].qty == 5);

    REQUIRE(book.best_ask() == 103);
    REQUIRE(book.depth_at(Side::Sell, 103) == 5);
    REQUIRE(book.depth_at(Side::Sell, 104) == 10);
    REQUIRE_FALSE(book.best_bid().has_value());   // taker fully filled
    REQUIRE(book.order_count() == 2);
}

TEST_CASE("sell side sweeps bids symmetrically", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);
    add_checked(book, 2, Side::Buy, 99, 10);
    add_checked(book, 3, Side::Buy, 98, 10);

    auto trades = add_checked(book, 4, Side::Sell, 99, 15);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].maker_id == 1); CHECK(trades[0].price == 100);
    CHECK(trades[1].maker_id == 2); CHECK(trades[1].price == 99); CHECK(trades[1].qty == 5);
    REQUIRE(book.best_bid() == 99);
    REQUIRE(book.depth_at(Side::Buy, 99) == 5);
    REQUIRE(book.depth_at(Side::Buy, 98) == 10);
}

TEST_CASE("non-crossing orders rest on both sides", "[phase1][match]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 99, 10);
    auto trades = add_checked(book, 2, Side::Sell, 100, 10);   // 100 > 99: no cross
    REQUIRE(trades.empty());
    REQUIRE(book.best_bid() == 99);
    REQUIRE(book.best_ask() == 100);
    REQUIRE(book.order_count() == 2);
}

// ---------------------------------------------------------------------------
TEST_CASE("time priority within a price level is FIFO", "[phase1][priority]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 105, 10);
    add_checked(book, 2, Side::Sell, 105, 10);
    add_checked(book, 3, Side::Sell, 105, 10);

    auto trades = add_checked(book, 4, Side::Buy, 105, 15);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].maker_id == 1); CHECK(trades[0].qty == 10);
    CHECK(trades[1].maker_id == 2); CHECK(trades[1].qty == 5);
    REQUIRE(book.depth_at(Side::Sell, 105) == 15);   // 5 left on #2, 10 on #3

    // Next taker should continue with the remainder of #2, then #3.
    trades = add_checked(book, 5, Side::Buy, 105, 10);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].maker_id == 2); CHECK(trades[0].qty == 5);
    CHECK(trades[1].maker_id == 3); CHECK(trades[1].qty == 5);
}

TEST_CASE("price priority beats time priority", "[phase1][priority]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 101, 10);   // arrived first, worse price
    add_checked(book, 2, Side::Sell, 100, 10);   // arrived later, better price
    auto trades = add_checked(book, 3, Side::Buy, 101, 10);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_id == 2);
}

// ---------------------------------------------------------------------------
TEST_CASE("cancel resting order removes it and its level if empty", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);
    add_checked(book, 2, Side::Buy, 99, 10);

    REQUIRE(book.cancel(1));
    require_not_crossed(book);
    REQUIRE(book.order_count() == 1);
    REQUIRE(book.depth_at(Side::Buy, 100) == 0);
    REQUIRE(book.best_bid() == 99);   // the empty level must be gone

    REQUIRE(book.cancel(2));
    REQUIRE(book.order_count() == 0);
    REQUIRE_FALSE(book.best_bid().has_value());
}

TEST_CASE("cancel one of several orders at a level keeps the others in order", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 10);
    add_checked(book, 2, Side::Sell, 100, 10);
    add_checked(book, 3, Side::Sell, 100, 10);

    REQUIRE(book.cancel(2));   // middle of the list
    REQUIRE(book.depth_at(Side::Sell, 100) == 20);
    REQUIRE(book.order_count() == 2);

    auto trades = add_checked(book, 4, Side::Buy, 100, 20);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].maker_id == 1);
    CHECK(trades[1].maker_id == 3);
}

TEST_CASE("cancel unknown id returns false and changes nothing", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);
    REQUIRE_FALSE(book.cancel(42));
    REQUIRE(book.order_count() == 1);
    REQUIRE(book.depth_at(Side::Buy, 100) == 10);
}

TEST_CASE("cancel after full fill returns false", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 10);
    add_checked(book, 2, Side::Buy, 100, 10);   // fills #1 completely
    REQUIRE(book.order_count() == 0);
    REQUIRE_FALSE(book.cancel(1));
    REQUIRE_FALSE(book.cancel(2));
}

TEST_CASE("cancel after partial fill removes the remainder", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Sell, 100, 10);
    add_checked(book, 2, Side::Buy, 100, 4);    // #1 has 6 left
    REQUIRE(book.depth_at(Side::Sell, 100) == 6);
    REQUIRE(book.cancel(1));
    REQUIRE(book.order_count() == 0);
    REQUIRE_FALSE(book.best_ask().has_value());
}

TEST_CASE("cancelled id can be reused", "[phase1][cancel]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);
    REQUIRE(book.cancel(1));
    auto trades = add_checked(book, 1, Side::Sell, 200, 3);
    REQUIRE(trades.empty());
    REQUIRE(book.best_ask() == 200);
}

// ---------------------------------------------------------------------------
TEST_CASE("duplicate id is rejected and book unchanged", "[phase1][reject]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);

    auto trades = add_checked(book, 1, Side::Sell, 90, 5);   // would cross if accepted
    REQUIRE(trades.empty());
    REQUIRE(book.order_count() == 1);
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.depth_at(Side::Buy, 100) == 10);
    REQUIRE_FALSE(book.best_ask().has_value());
}

TEST_CASE("zero quantity is rejected", "[phase1][reject]") {
    OrderBook book;
    auto trades = add_checked(book, 1, Side::Buy, 100, 0);
    REQUIRE(trades.empty());
    REQUIRE(book.order_count() == 0);
    REQUIRE_FALSE(book.best_bid().has_value());
    REQUIRE_FALSE(book.cancel(1));   // it never rested
}

// ---------------------------------------------------------------------------
TEST_CASE("book is never crossed after a mixed sequence", "[phase1][invariant]") {
    OrderBook book;
    add_checked(book, 1, Side::Buy, 100, 10);
    add_checked(book, 2, Side::Sell, 102, 10);
    add_checked(book, 3, Side::Buy, 101, 5);
    add_checked(book, 4, Side::Sell, 101, 10);   // crosses #3, rests 5 at 101
    REQUIRE(book.best_bid() == 100);
    REQUIRE(book.best_ask() == 101);
    add_checked(book, 5, Side::Buy, 105, 30);    // sweeps 101 and 102, rests 15 at 105
    REQUIRE(book.best_bid() == 105);
    REQUIRE_FALSE(book.best_ask().has_value());
    REQUIRE(book.cancel(5));
    require_not_crossed(book);
    add_checked(book, 6, Side::Sell, 100, 10);   // crosses #1 exactly
    REQUIRE(book.order_count() == 0);
}
