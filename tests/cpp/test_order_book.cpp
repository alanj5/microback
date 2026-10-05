#include "microback/order_book.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <random>
#include <stdexcept>
using namespace microback;
namespace {
Order order(std::uint64_t id, Side side, std::int64_t price, std::int64_t qty) {
    return {OrderId{id}, side, PriceTicks{price}, QuantityLots{qty}};
}
} // namespace
TEST_CASE("Empty and one-sided book metrics are explicit") {
    OrderBook b;
    REQUIRE(b.valid());
    REQUIRE_FALSE(b.best_bid());
    REQUIRE_FALSE(b.best_ask());
    REQUIRE_FALSE(b.spread_ticks());
    REQUIRE_FALSE(b.mid_price());
    REQUIRE_FALSE(b.microprice());
    REQUIRE(b.imbalance(5) == 0);
    REQUIRE(b.depth(Side::Buy, 5).empty());
    b.add(order(1, Side::Buy, 100, 10));
    REQUIRE(b.best_bid() == PriceTicks{100});
    REQUIRE_FALSE(b.midpoint());
    REQUIRE(b.imbalance(5) == 1);
    REQUIRE(b.valid());
    b.clear();
    REQUIRE(b.order_count() == 0);
    REQUIRE(b.valid());
    b.add(order(2, Side::Sell, 110, 20));
    REQUIRE(b.best_ask() == PriceTicks{110});
    REQUIRE(b.imbalance(5) == -1);
}
TEST_CASE("Bid ask ordering depth and weighted metrics") {
    OrderBook b;
    b.add(order(1, Side::Buy, 100, 30));
    b.add(order(2, Side::Buy, 99, 10));
    b.add(order(3, Side::Sell, 103, 10));
    b.add(order(4, Side::Sell, 104, 20));
    REQUIRE(b.valid());
    REQUIRE(b.spread_ticks() == PriceTicks{3});
    REQUIRE(b.mid_price() == PriceTicks{101});
    REQUIRE(b.midpoint() == Midpoint{PriceTicks{101}, true});
    REQUIRE(b.midpoint().value_or(Midpoint{}).as_double() == 101.5);
    REQUIRE(b.microprice() == PriceTicks{102});
    REQUIRE(b.imbalance(1) == Catch::Approx(0.5));
    REQUIRE(b.imbalance(2) == Catch::Approx(1.0 / 7));
    REQUIRE(b.imbalance(0) == 0);
    REQUIRE(b.depth(Side::Buy, 5) ==
            DepthView{{PriceTicks{100}, QuantityLots{30}}, {PriceTicks{99}, QuantityLots{10}}});
    REQUIRE(b.depth(Side::Sell, 1) == DepthView{{PriceTicks{103}, QuantityLots{10}}});
    REQUIRE(b.depth(Side::Buy, 0).empty());
    REQUIRE(b.vwap(Side::Sell, QuantityLots{20}) == PriceTicks{103});
    REQUIRE(b.vwap(Side::Buy, QuantityLots{40}) == PriceTicks{99});
    REQUIRE_FALSE(b.vwap(Side::Sell, QuantityLots{31}));
    REQUIRE_THROWS_AS(b.vwap(Side::Buy, QuantityLots{0}), std::invalid_argument);
}
TEST_CASE("FIFO and indexed first middle last cancellation") {
    for (auto side : {Side::Buy, Side::Sell}) {
        OrderBook b;
        for (std::uint64_t i = 1; i <= 4; ++i)
            b.add(order(i, side, 100, 10));
        auto q = b.orders_at(side, PriceTicks{100});
        REQUIRE(q[0].id == OrderId{1});
        REQUIRE(q[3].id == OrderId{4});
        b.cancel(OrderId{1});
        REQUIRE(b.orders_at(side, PriceTicks{100}).front().id == OrderId{2});
        b.cancel(OrderId{3});
        REQUIRE(b.depth(side, 1).front().quantity == QuantityLots{20});
        b.cancel(OrderId{4});
        REQUIRE(b.orders_at(side, PriceTicks{100}).size() == 1);
        b.cancel(OrderId{2});
        REQUIRE(b.depth(side, 1).empty());
        REQUIRE(b.valid());
        REQUIRE_THROWS_AS(b.cancel(OrderId{2}), std::invalid_argument);
    }
}
TEST_CASE("Quantity priority and price replacement semantics") {
    for (auto side : {Side::Buy, Side::Sell}) {
        OrderBook b;
        b.add(order(1, side, 100, 10));
        b.add(order(2, side, 100, 10));
        b.modify({OrderId{1}, PriceTicks{100}, QuantityLots{5}});
        REQUIRE(b.orders_at(side, PriceTicks{100}).front().id == OrderId{1});
        b.modify({OrderId{1}, PriceTicks{100}, QuantityLots{5}});
        REQUIRE(b.orders_at(side, PriceTicks{100}).front().id == OrderId{1});
        b.modify({OrderId{1}, PriceTicks{100}, QuantityLots{6}});
        REQUIRE(b.orders_at(side, PriceTicks{100}).back().id == OrderId{1});
        b.modify({OrderId{1}, PriceTicks{101}, QuantityLots{7}});
        REQUIRE(b.depth(side, 5).size() == 2);
        REQUIRE(b.find(OrderId{1}).value_or(Order{}).price == PriceTicks{101});
        b.modify({OrderId{2}, PriceTicks{101}, QuantityLots{10}});
        REQUIRE(b.depth(side, 5).size() == 1);
        REQUIRE(b.orders_at(side, PriceTicks{101}).back().id == OrderId{2});
        REQUIRE(b.valid());
    }
}
TEST_CASE("Malformed operations and overflow leave book intact") {
    OrderBook b;
    b.add(order(1, Side::Buy, 100, 10));
    REQUIRE_THROWS_AS(b.add(order(1, Side::Sell, 110, 10)), std::invalid_argument);
    REQUIRE_THROWS_AS(b.add(order(0, Side::Buy, 100, 1)), std::invalid_argument);
    REQUIRE_THROWS_AS(b.add(order(2, Side::Buy, 0, 1)), std::invalid_argument);
    REQUIRE_THROWS_AS(b.add(order(2, Side::Buy, 100, -1)), std::invalid_argument);
    REQUIRE_THROWS_AS(b.add(order(2, static_cast<Side>(9), 100, 1)), std::invalid_argument);
    REQUIRE_THROWS_AS(b.modify({OrderId{2}, PriceTicks{100}, QuantityLots{1}}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(b.modify({OrderId{1}, PriceTicks{100}, QuantityLots{0}}),
                      std::invalid_argument);
    REQUIRE_FALSE(b.find(OrderId{99}));
    REQUIRE(b.order_count() == 1);
    REQUIRE(b.valid());
    const auto max = std::numeric_limits<std::int64_t>::max();
    b.add(order(2, Side::Buy, 99, max));
    REQUIRE_THROWS_AS(b.add(order(3, Side::Buy, 99, 1)), std::overflow_error);
    REQUIRE_THROWS_AS(b.modify({OrderId{1}, PriceTicks{99}, QuantityLots{1}}), std::overflow_error);
    REQUIRE(b.find(OrderId{1}).value_or(Order{}).quantity == QuantityLots{10});
    REQUIRE(b.valid());
    b.add(order(3, Side::Buy, 100, 1));
    REQUIRE_THROWS_AS(b.modify({OrderId{1}, PriceTicks{100}, QuantityLots{max}}),
                      std::overflow_error);
    REQUIRE(b.valid());
}
TEST_CASE("Extreme prices retain integer metric safety") {
    const auto max = std::numeric_limits<std::int64_t>::max();
    OrderBook b;
    b.add(order(1, Side::Buy, max - 2, max));
    b.add(order(2, Side::Sell, max, max));
    REQUIRE(b.valid());
    REQUIRE(b.mid_price() == PriceTicks{max - 1});
    REQUIRE(b.microprice() == PriceTicks{max - 1});
    REQUIRE(b.vwap(Side::Sell, QuantityLots{max}) == PriceTicks{max});
    OrderBook crossed;
    crossed.add(order(1, Side::Buy, 100, 1));
    crossed.add(order(2, Side::Sell, 100, 1));
    REQUIRE_FALSE(crossed.valid());
    REQUIRE(crossed.spread_ticks() == PriceTicks{0});
}
TEST_CASE("Seeded mutation stream preserves aggregates and index") {
    OrderBook b;
    // Fixed seed deliberately makes this invariant regression reproducible.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937_64 rng{42};
    for (std::uint64_t i = 1; i <= 500; ++i) {
        const auto price = 90 + static_cast<std::int64_t>(rng() % 10);
        b.add(order(i, Side::Buy, price, 1 + static_cast<std::int64_t>(rng() % 100)));
        REQUIRE(b.valid());
    }
    for (std::uint64_t i = 1; i <= 500; ++i) {
        b.modify({OrderId{i}, PriceTicks{90 + static_cast<std::int64_t>(rng() % 10)},
                  QuantityLots{1 + static_cast<std::int64_t>(rng() % 100)}});
        REQUIRE(b.valid());
    }
    for (std::uint64_t i = 1; i <= 500; ++i) {
        b.cancel(OrderId{i});
        REQUIRE(b.valid());
    }
    REQUIRE(b.order_count() == 0);
}
