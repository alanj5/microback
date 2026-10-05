#include "microback/execution.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stdexcept>
using namespace microback;
namespace {
MarketEvent ev(std::int64_t t, std::uint64_t seq, std::uint64_t id, std::int64_t p, std::int64_t q,
               MarketEventType type, Side side = Side::Buy) {
    return {Timestamp{t}, Sequence{seq}, OrderId{id}, PriceTicks{p}, QuantityLots{q}, type, side};
}
void init(ExecutionEngine &e) {
    e.on_market_event(ev(0, 1, 0, 0, 0, MarketEventType::Snapshot));
    e.on_market_event(ev(0, 2, 1, 100, 10, MarketEventType::Snapshot));
    e.on_market_event(ev(0, 3, 2, 102, 10, MarketEventType::Snapshot, Side::Sell));
    e.on_market_event(ev(0, 4, 3, 103, 20, MarketEventType::Snapshot, Side::Sell));
    e.on_market_event(ev(0, 5, 0, 0, 0, MarketEventType::Snapshot, Side::Sell));
}
const SimOrder &order(const ExecutionEngine &e, OrderId id) { return e.orders().at(id); }
} // namespace
TEST_CASE("Market orders walk depth and reconcile insufficient liquidity") {
    ExecutionEngine e;
    init(e);
    const auto id = e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{35});
    REQUIRE(order(e, id).status == OrderStatus::Pending);
    REQUIRE(e.fills().empty());
    e.flush();
    REQUIRE(e.fills().size() == 2);
    REQUIRE(e.fills()[0].price == PriceTicks{102});
    REQUIRE(e.fills()[0].quantity == QuantityLots{10});
    REQUIRE(e.fills()[1].price == PriceTicks{103});
    REQUIRE(e.fills()[1].quantity == QuantityLots{20});
    REQUIRE(order(e, id).filled_quantity == QuantityLots{30});
    REQUIRE(order(e, id).cancelled_quantity == QuantityLots{5});
    REQUIRE(order(e, id).status == OrderStatus::Cancelled);
    REQUIRE(e.valid());
    REQUIRE(e.book().find(OrderId{2}).value_or(Order{}).quantity == QuantityLots{10});
    const auto next = e.submit(Side::Buy, OrderType::Market, TimeInForce::GTC, QuantityLots{1});
    e.flush();
    REQUIRE(order(e, next).cancelled_quantity == QuantityLots{1});
    REQUIRE(e.fills().size() == 2);
    REQUIRE(e.valid());
}
TEST_CASE("Single-level sells and configured taker fees") {
    ExecutionEngine e{{Timestamp{0}, QueueModel::Conservative, -100, 200}};
    init(e);
    const auto id = e.submit(Side::Sell, OrderType::Market, TimeInForce::IOC, QuantityLots{3});
    e.flush();
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.fills()[0].price == PriceTicks{100});
    REQUIRE(e.fills()[0].fee_atoms == static_cast<MoneyAtoms>(60000));
    REQUIRE(e.fills()[0].liquidity == Liquidity::Taker);
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    REQUIRE(e.valid());
}
TEST_CASE("Crossing limits respect price and IOC cancels remainder") {
    ExecutionEngine e;
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::IOC, QuantityLots{15}, PriceTicks{102});
    e.flush();
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.fills()[0].quantity == QuantityLots{10});
    REQUIRE(order(e, id).cancelled_quantity == QuantityLots{5});
    REQUIRE(e.valid());
    const auto passive =
        e.submit(Side::Sell, OrderType::Limit, TimeInForce::IOC, QuantityLots{2}, PriceTicks{104});
    e.flush();
    REQUIRE(order(e, passive).status == OrderStatus::Cancelled);
    REQUIRE(e.fills().size() == 1);
}
TEST_CASE("GTC remainder rests and later receives maker economics") {
    ExecutionEngine e{{Timestamp{0}, QueueModel::Conservative, -100, 200}};
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{15}, PriceTicks{102});
    e.flush();
    REQUIRE(order(e, id).status == OrderStatus::PartiallyFilled);
    REQUIRE(order(e, id).remaining_quantity == QuantityLots{5});
    e.on_market_event(ev(10, 6, 0, 102, 5, MarketEventType::Trade, Side::Buy));
    REQUIRE(e.fills().size() == 2);
    REQUIRE(e.fills()[0].liquidity == Liquidity::Taker);
    REQUIRE(e.fills()[1].liquidity == Liquidity::Maker);
    REQUIRE(e.fills()[1].fee_atoms == static_cast<MoneyAtoms>(-51000));
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    REQUIRE(e.valid());
}
TEST_CASE("Passive queue must exhaust earlier visible lots before filling") {
    ExecutionEngine e;
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{3}, PriceTicks{100});
    e.flush();
    REQUIRE(order(e, id).historical_ahead == QuantityLots{10});
    e.on_market_event(ev(1, 6, 0, 100, 8, MarketEventType::Trade));
    REQUIRE(e.fills().empty());
    e.on_market_event(ev(2, 7, 0, 100, 3, MarketEventType::Trade));
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.fills()[0].quantity == QuantityLots{1});
    REQUIRE(order(e, id).status == OrderStatus::PartiallyFilled);
    e.on_market_event(ev(3, 8, 0, 100, 2, MarketEventType::Trade));
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    REQUIRE(e.valid());
}
TEST_CASE("Each queue cancellation model has an explicit outcome") {
    for (auto model : {QueueModel::Conservative, QueueModel::Neutral, QueueModel::Optimistic}) {
        ExecutionEngine e{{Timestamp{0}, model, 0, 0}};
        init(e);
        const auto id = e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2},
                                 PriceTicks{100});
        e.flush();
        // New additions are behind our queue; only cancellation assumptions alter ahead.
        e.on_market_event(ev(1, 6, 1, 100, 20, MarketEventType::Modify));
        e.on_market_event(ev(2, 7, 1, 100, 10, MarketEventType::Modify));
        const auto expected =
            model == QueueModel::Conservative ? 10 : (model == QueueModel::Neutral ? 5 : 0);
        REQUIRE(order(e, id).historical_ahead == QuantityLots{expected});
        e.on_market_event(ev(3, 8, 0, 100, 2, MarketEventType::Trade));
        REQUIRE((order(e, id).status == OrderStatus::Filled) == (model == QueueModel::Optimistic));
        REQUIRE(e.valid());
    }
}
TEST_CASE("L2 trade plus depth change is never double-counted as cancellation") {
    ExecutionEngine e{{Timestamp{0}, QueueModel::Optimistic, 0, 0}};
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{1}, PriceTicks{100});
    e.flush();
    e.on_market_event(ev(1, 6, 0, 100, 4, MarketEventType::Trade));
    REQUIRE(order(e, id).historical_ahead == QuantityLots{6});
    e.on_market_event(ev(2, 7, 1, 100, 6, MarketEventType::Modify));
    REQUIRE(order(e, id).historical_ahead == QuantityLots{6});
    REQUIRE(e.fills().empty());
    REQUIRE(e.valid());
}
TEST_CASE("Client FIFO prevents duplicate passive fills and cancellation releases ahead") {
    ExecutionEngine e;
    init(e);
    const auto first =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2}, PriceTicks{99});
    const auto second =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2}, PriceTicks{99});
    e.flush();
    REQUIRE(order(e, second).client_ahead == QuantityLots{2});
    e.on_market_event(ev(1, 6, 0, 99, 3, MarketEventType::Trade));
    REQUIRE(order(e, first).filled_quantity == QuantityLots{2});
    REQUIRE(order(e, second).filled_quantity == QuantityLots{1});
    REQUIRE(e.valid());
    ExecutionEngine c;
    init(c);
    const auto a =
        c.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2}, PriceTicks{99});
    const auto b =
        c.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2}, PriceTicks{99});
    c.flush();
    c.cancel(a);
    c.flush();
    REQUIRE(order(c, b).client_ahead == QuantityLots{0});
    c.on_market_event(ev(1, 6, 0, 99, 2, MarketEventType::Trade));
    REQUIRE(order(c, b).status == OrderStatus::Filled);
    REQUIRE(c.valid());
}
TEST_CASE("Latency blocks unrealistically favourable stale-book fills") {
    for (auto type : {OrderType::Market, OrderType::Limit}) {
        ExecutionEngine e{{Timestamp{100}, QueueModel::Conservative, 0, 0}};
        init(e);
        const auto id = e.submit(Side::Buy, type, TimeInForce::GTC, QuantityLots{1},
                                 type == OrderType::Limit ? PriceTicks{102} : PriceTicks{});
        e.flush();
        REQUIRE(e.fills().empty());
        e.on_market_event(ev(50, 6, 2, 110, 10, MarketEventType::Modify, Side::Sell));
        REQUIRE(e.fills().empty());
        REQUIRE(order(e, id).status == OrderStatus::Pending);
        e.on_market_event(ev(100, 7, 3, 111, 20, MarketEventType::Modify, Side::Sell));
        if (type == OrderType::Market) {
            REQUIRE(e.fills().size() == 1);
            REQUIRE(e.fills()[0].timestamp == Timestamp{100});
            REQUIRE(e.fills()[0].price == PriceTicks{110});
            REQUIRE(e.fills()[0].decision_mid == Midpoint{PriceTicks{101}, false});
            REQUIRE(e.fills()[0].arrival_mid == Midpoint{PriceTicks{105}, false});
        } else {
            REQUIRE(e.fills().empty());
            REQUIRE(order(e, id).status == OrderStatus::Active);
        }
        REQUIRE(e.valid());
    }
}
TEST_CASE("Arrival between external events uses only previously observable book") {
    ExecutionEngine e{{Timestamp{50}, QueueModel::Conservative, 0, 0}};
    init(e);
    e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1});
    e.on_market_event(ev(100, 6, 2, 110, 10, MarketEventType::Modify, Side::Sell));
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.fills()[0].timestamp == Timestamp{50});
    REQUIRE(e.fills()[0].price == PriceTicks{102});
    REQUIRE(e.now() == Timestamp{100});
}
TEST_CASE("Cancellation latency and trade-at-arrival ties are causal") {
    ExecutionEngine e{{Timestamp{10}, QueueModel::Conservative, 0, 0}};
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{1}, PriceTicks{99});
    e.on_market_event(ev(10, 6, 0, 99, 1, MarketEventType::Trade));
    REQUIRE(e.fills().empty());
    REQUIRE(order(e, id).status == OrderStatus::Active);
    e.cancel(id);
    e.on_market_event(ev(15, 7, 0, 99, 1, MarketEventType::Trade));
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    e.on_market_event(ev(20, 8, 0, 200, 1, MarketEventType::Trade));
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.valid());
}
TEST_CASE("Snapshot resets cancel resting clients and final horizon does not invent fills") {
    ExecutionEngine e;
    init(e);
    const auto id =
        e.submit(Side::Sell, OrderType::Limit, TimeInForce::GTC, QuantityLots{1}, PriceTicks{104});
    e.flush();
    e.on_market_event(ev(10, 6, 0, 0, 0, MarketEventType::Snapshot));
    REQUIRE_THROWS_AS(e.flush(), std::logic_error);
    REQUIRE_THROWS_AS(e.cancel(id), std::logic_error);
    REQUIRE_THROWS_AS(e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1}),
                      std::logic_error);
    e.on_market_event(ev(10, 7, 0, 0, 0, MarketEventType::Snapshot, Side::Sell));
    REQUIRE(order(e, id).status == OrderStatus::Cancelled);
    REQUIRE(e.valid());
    ExecutionEngine pending{{Timestamp{100}, QueueModel::Conservative, 0, 0}};
    init(pending);
    const auto p = pending.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1});
    InMemoryEventSource empty{{}};
    pending.run(empty);
    REQUIRE(order(pending, p).status == OrderStatus::Pending);
    REQUIRE(pending.fills().empty());
}
TEST_CASE("Execution config and invalid requests are rejected") {
    REQUIRE_THROWS_AS(ConstantLatency{Timestamp{-1}}, std::invalid_argument);
    REQUIRE_THROWS_AS(
        (ExecutionEngine{ExecutionConfig{Timestamp{0}, static_cast<QueueModel>(8), 0, 0}}),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        (ExecutionEngine{ExecutionConfig{Timestamp{0}, QueueModel::Neutral, 0, 1000001}}),
        std::invalid_argument);
    ExecutionEngine e;
    REQUIRE_THROWS_AS(e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1}),
                      std::logic_error);
    init(e);
    REQUIRE_THROWS_AS(e.cancel(OrderId{99}), std::invalid_argument);
    REQUIRE_THROWS_AS(e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(e.submit(Side::Buy, OrderType::Limit, TimeInForce::IOC, QuantityLots{1}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(
        e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1}, PriceTicks{10}),
        std::invalid_argument);
    REQUIRE(e.orders().empty());
}
TEST_CASE("Identical full execution runs produce identical orders and fills") {
    auto run = [](ExecutionEngine &e) {
        init(e);
        e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{2});
        e.flush();
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{1}, PriceTicks{99});
        e.flush();
        e.on_market_event(ev(10, 6, 0, 99, 1, MarketEventType::Trade));
        REQUIRE(e.valid());
    };
    ExecutionEngine a, b;
    run(a);
    run(b);
    REQUIRE(a.orders() == b.orders());
    REQUIRE(a.fills() == b.fills());
}

TEST_CASE("Printed trades cannot provide stale taker liquidity at arrival") {
    ExecutionEngine e{{Timestamp{10}, QueueModel::Conservative, 0, 0}};
    init(e);
    e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1});
    e.on_market_event(ev(10, 6, 0, 102, 10, MarketEventType::Trade, Side::Sell));
    REQUIRE(e.fills().size() == 1);
    REQUIRE(e.fills()[0].price == PriceTicks{103});
    REQUIRE(e.valid());
}
TEST_CASE("Taker consumption persists until historical replenishment") {
    ExecutionEngine e;
    init(e);
    e.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{10});
    e.flush();
    e.on_market_event(ev(10, 6, 2, 102, 15, MarketEventType::Modify, Side::Sell));
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::IOC, QuantityLots{8}, PriceTicks{102});
    e.flush();
    REQUIRE(order(e, id).filled_quantity == QuantityLots{5});
    REQUIRE(e.fills().back().price == PriceTicks{102});
    REQUIRE(e.valid());
}

TEST_CASE("Neutral cancellation uses the pool after already-observed trades") {
    ExecutionEngine e{{Timestamp{0}, QueueModel::Neutral, 0, 0}};
    init(e);
    const auto id =
        e.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{1}, PriceTicks{100});
    e.flush();
    e.on_market_event(ev(1, 6, 0, 100, 4, MarketEventType::Trade));
    e.on_market_event(ev(2, 7, 1, 100, 4, MarketEventType::Modify));
    REQUIRE(order(e, id).historical_ahead == QuantityLots{4});
    REQUIRE(e.fills().empty());
    REQUIRE(e.valid());
}
TEST_CASE("Sell crossings and positive maker fees use the correct liquidity label") {
    ExecutionEngine e{{Timestamp{0}, QueueModel::Conservative, 100, 200}};
    init(e);
    e.submit(Side::Sell, OrderType::Limit, TimeInForce::IOC, QuantityLots{2}, PriceTicks{99});
    e.flush();
    REQUIRE(e.fills()[0].price == PriceTicks{100});
    REQUIRE(e.fills()[0].liquidity == Liquidity::Taker);
    const auto id =
        e.submit(Side::Sell, OrderType::Limit, TimeInForce::GTC, QuantityLots{2}, PriceTicks{104});
    e.flush();
    e.on_market_event(ev(1, 6, 0, 104, 2, MarketEventType::Trade, Side::Sell));
    REQUIRE(order(e, id).status == OrderStatus::Filled);
    REQUIRE(e.fills().back().fee_atoms == static_cast<MoneyAtoms>(20800));
    REQUIRE(e.fills().back().liquidity == Liquidity::Maker);
    REQUIRE(e.valid());
}
TEST_CASE("Overflow and malformed data leave execution unusable") {
    ExecutionEngine e;
    init(e);
    REQUIRE_THROWS(e.on_market_event(ev(1, 4, 0, 100, 1, MarketEventType::Trade)));
    REQUIRE_THROWS_AS(e.flush(), std::logic_error);
    InMemoryEventSource empty{{}};
    REQUIRE_THROWS_AS(e.run(empty), std::logic_error);
    ExecutionEngine overflow{
        {Timestamp{std::numeric_limits<std::int64_t>::max()}, QueueModel::Conservative, 0, 0}};
    init(overflow);
    overflow.on_market_event(ev(1, 6, 0, 200, 1, MarketEventType::Trade));
    REQUIRE_THROWS_AS(
        overflow.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1}),
        std::overflow_error);
    REQUIRE(overflow.orders().empty());
}

TEST_CASE("Finish preserves sticky execution failure even after a complete snapshot") {
    ExecutionEngine engine;
    CsvEventSource source{std::string{MICROBACK_FIXTURE_DIR} + "/replay.csv"};
    engine.run(source);
    REQUIRE_THROWS(engine.on_market_event({Timestamp{0}, Sequence{100}, OrderId{0}, PriceTicks{1},
                                           QuantityLots{1}, MarketEventType::Trade, Side::Buy}));
    REQUIRE_THROWS(engine.finish());
}
