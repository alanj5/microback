#include "microback/strategy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <type_traits>
using namespace microback;
static_assert(std::is_same_v<decltype(std::declval<Context>().book()), const OrderBook &>);
namespace {
std::vector<MarketEvent> events() {
    return {{Timestamp{0}, Sequence{1}, OrderId{0}, PriceTicks{0}, QuantityLots{0},
             MarketEventType::Snapshot, Side::Buy},
            {Timestamp{0}, Sequence{2}, OrderId{1}, PriceTicks{100}, QuantityLots{30},
             MarketEventType::Snapshot, Side::Buy},
            {Timestamp{0}, Sequence{3}, OrderId{2}, PriceTicks{102}, QuantityLots{10},
             MarketEventType::Snapshot, Side::Sell},
            {Timestamp{0}, Sequence{4}, OrderId{0}, PriceTicks{0}, QuantityLots{0},
             MarketEventType::Snapshot, Side::Sell},
            {Timestamp{10}, Sequence{5}, OrderId{0}, PriceTicks{100}, QuantityLots{31},
             MarketEventType::Trade, Side::Buy},
            {Timestamp{20}, Sequence{6}, OrderId{1}, PriceTicks{100}, QuantityLots{10},
             MarketEventType::Modify, Side::Buy}};
}
class Probe : public Strategy {
  public:
    int starts = 0, markets = 0, fills = 0, timers = 0, ends = 0;
    void on_start(Context &ctx) override {
        ++starts;
        ctx.submit_market(Side::Buy, QuantityLots{1});
    }
    void on_market_event(Context &, const MarketEvent &) override { ++markets; }
    void on_fill(Context &ctx, const Fill &f) override {
        ++fills;
        REQUIRE(ctx.now() == f.timestamp);
        REQUIRE(ctx.portfolio().state().position == QuantityLots{1});
    }
    void on_timer(Context &, Timestamp) override { ++timers; }
    void on_end(Context &) override { ++ends; }
};
} // namespace
TEST_CASE("Strategy hooks timers fills and readonly context obey event boundaries") {
    Probe p;
    InMemoryEventSource source{events()};
    RunConfig cfg;
    cfg.timer_interval = Timestamp{5};
    cfg.execution.latency_ns = Timestamp{3};
    const auto result = run_backtest(source, p, cfg);
    REQUIRE(p.starts == 1);
    REQUIRE(p.ends == 1);
    REQUIRE(p.markets == 3);
    REQUIRE(p.fills == 1);
    REQUIRE(p.timers == 4);
    REQUIRE(result.fills[0].timestamp == Timestamp{3});
    REQUIRE(result.fills[0].price == PriceTicks{102});
}
TEST_CASE("Imbalance actions and full results are deterministic") {
    ImbalanceStrategy a, b;
    InMemoryEventSource x{events()}, y{events()};
    const auto first = run_backtest(x, a), second = run_backtest(y, b);
    REQUIRE(first == second);
    REQUIRE(first.orders.size() == 2);
    REQUIRE(first.fills[0].side == Side::Buy);
    REQUIRE(first.fills[0].quantity == QuantityLots{1});
    REQUIRE(first.orders.back().side == Side::Sell);
    // Observed trades exhausted visible liquidity: the flatten request cannot invent a fill.
    REQUIRE(first.orders.back().status == OrderStatus::Cancelled);
}
TEST_CASE("Market maker changes quotes with inventory and waits for cancellation") {
    MarketMaker maker;
    InMemoryEventSource source{events()};
    const auto result = run_backtest(source, maker);
    REQUIRE(result.orders.size() >= 3);
    REQUIRE_FALSE(result.fills.empty());
    REQUIRE(result.fills[0].liquidity == Liquidity::Maker);
    bool skewed = false;
    for (const auto &o : result.orders)
        if (o.side == Side::Buy && o.limit_price == PriceTicks{99})
            skewed = true;
    REQUIRE(skewed);
}
TEST_CASE("Strategy parameter validation rejects inconsistent thresholds") {
    REQUIRE_THROWS(ImbalanceStrategy(ImbalanceParameters{0, 0.35, 0.1, QuantityLots{1}}));
    REQUIRE_THROWS(MarketMaker(MakerParameters{QuantityLots{0}}));
}
