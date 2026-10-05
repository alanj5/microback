#include "microback/execution.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace microback;
namespace {
Fill f(Side side, std::int64_t price, std::int64_t qty, MoneyAtoms fee = 0) {
    return {OrderId{1},
            Timestamp{1},
            side,
            PriceTicks{price},
            QuantityLots{qty},
            Liquidity::Taker,
            fee,
            Midpoint{PriceTicks{100}, true},
            Midpoint{PriceTicks{101}, false}};
}
MarketEvent e(std::uint64_t seq, std::uint64_t id, std::int64_t p, std::int64_t q,
              MarketEventType type, Side side = Side::Buy, std::int64_t time = 0) {
    return {Timestamp{time}, Sequence{seq}, OrderId{id}, PriceTicks{p},
            QuantityLots{q}, type,          side};
}
void init(ExecutionEngine &engine) {
    engine.on_market_event(e(1, 0, 0, 0, MarketEventType::Snapshot));
    engine.on_market_event(e(2, 1, 100, 10, MarketEventType::Snapshot));
    engine.on_market_event(e(3, 2, 102, 10, MarketEventType::Snapshot, Side::Sell));
    engine.on_market_event(e(4, 0, 0, 0, MarketEventType::Snapshot, Side::Sell));
}
} // namespace
TEST_CASE("Profitable losing and short round trips reconcile exact cash PnL") {
    for (auto side : {Side::Buy, Side::Sell})
        for (auto exit : {90, 110}) {
            Portfolio p{1000 * money_scale};
            p.apply(f(side, 100, 3, 300));
            p.apply(f(side == Side::Buy ? Side::Sell : Side::Buy, exit, 3, -100));
            const auto expected =
                static_cast<MoneyAtoms>((side == Side::Buy ? exit - 100 : 100 - exit) * 3) *
                money_scale;
            const auto v = p.mark({PriceTicks{100}, false});
            REQUIRE(p.state().position == QuantityLots{0});
            REQUIRE(p.state().basis == 0);
            REQUIRE(p.state().realised_pnl == expected);
            REQUIRE(v.gross_pnl == expected);
            REQUIRE(v.net_pnl == expected - 200);
            REQUIRE(v.equity == 1000 * money_scale + expected - 200);
        }
}
TEST_CASE("Partial accounting and position reversal allocate entry basis") {
    Portfolio p;
    p.apply(f(Side::Buy, 100, 2));
    p.apply(f(Side::Buy, 110, 2));
    REQUIRE(p.average_entry_ticks() == 105);
    p.apply(f(Side::Sell, 120, 1));
    REQUIRE(p.state().realised_pnl == 15 * money_scale);
    REQUIRE(p.mark({PriceTicks{115}, false}).unrealised_pnl == 30 * money_scale);
    p.apply(f(Side::Sell, 90, 5));
    REQUIRE(p.state().position == QuantityLots{-2});
    REQUIRE(p.average_entry_ticks() == 90);
    REQUIRE(p.state().realised_pnl == -30 * money_scale);
    REQUIRE(p.mark({PriceTicks{80}, false}).gross_pnl == -10 * money_scale);
    REQUIRE(p.state().turnover == 990 * money_scale);
}
TEST_CASE("Half-tick marks fees rebates and signed slippage remain exact") {
    Portfolio p;
    p.apply(f(Side::Buy, 102, 2, 40800));
    const auto v = p.mark({PriceTicks{103}, true});
    REQUIRE(v.unrealised_pnl == 3 * money_scale);
    REQUIRE(v.net_pnl == 3 * money_scale - 40800);
    REQUIRE(v.exposure == 207 * money_scale);
    REQUIRE(p.state().decision_slippage == 3 * money_scale);
    REQUIRE(p.state().arrival_slippage == 2 * money_scale);
    p.apply(f(Side::Sell, 104, 2, -20800));
    REQUIRE(p.state().fees == 20000);
    REQUIRE(p.state().arrival_slippage == -4 * money_scale);
    REQUIRE(p.mark({PriceTicks{103}, false}).net_pnl == 4 * money_scale - 20000);
}
TEST_CASE("Risk rejects size position and notional without changing portfolio") {
    for (auto limits : {RiskLimits{QuantityLots{10}, QuantityLots{1}, 0},
                        RiskLimits{QuantityLots{1}, QuantityLots{10}, 0},
                        RiskLimits{QuantityLots{10}, QuantityLots{10}, 100 * money_scale}}) {
        ExecutionEngine engine{{}, limits};
        init(engine);
        const auto before = engine.portfolio().state();
        const auto id = engine.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC,
                                      QuantityLots{2}, PriceTicks{100});
        engine.flush();
        REQUIRE(engine.orders().at(id).status == OrderStatus::Rejected);
        REQUIRE(engine.rejections().size() == 1);
        REQUIRE(engine.portfolio().state() == before);
        REQUIRE(engine.valid());
    }
}
TEST_CASE("Pending exposure is reserved until exchange cancellation and fills account") {
    ExecutionEngine engine{{Timestamp{10}, QueueModel::Conservative, 0, 200},
                           {QuantityLots{2}, QuantityLots{2}, 0}};
    init(engine);
    const auto a = engine.submit(Side::Buy, OrderType::Limit, TimeInForce::GTC, QuantityLots{2},
                                 PriceTicks{99});
    const auto b = engine.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{1});
    REQUIRE(engine.orders().at(b).status == OrderStatus::Rejected);
    engine.cancel(a);
    engine.on_market_event(e(5, 0, 200, 1, MarketEventType::Trade, Side::Buy, 10));
    const auto c = engine.submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{2});
    engine.on_market_event(e(6, 0, 200, 1, MarketEventType::Trade, Side::Buy, 20));
    REQUIRE(engine.orders().at(c).status == OrderStatus::Filled);
    REQUIRE(engine.portfolio().state().position == QuantityLots{2});
    REQUIRE(engine.portfolio().state().cash == -204 * money_scale - 40800);
    REQUIRE(engine.valid());
}
TEST_CASE("Invalid accounting and risk configuration fail without mutation") {
    Portfolio p;
    const auto before = p.state();
    auto fill = f(Side::Buy, 100, 0);
    REQUIRE_THROWS(p.apply(fill));
    REQUIRE(p.state() == before);
    REQUIRE_THROWS(validate_limits({QuantityLots{-1}, QuantityLots{1}, 0}));
    REQUIRE_THROWS(money_mul(std::numeric_limits<MoneyAtoms>::max(), 2));
}
