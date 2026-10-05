#ifndef MICROBACK_STRATEGY_HPP
#define MICROBACK_STRATEGY_HPP
#include "microback/execution.hpp"
namespace microback {
class Context {
    ExecutionEngine &engine_;

  public:
    explicit Context(ExecutionEngine &engine) : engine_(engine) {}
    [[nodiscard]] const OrderBook &book() const { return engine_.book(); }
    [[nodiscard]] const Portfolio &portfolio() const { return engine_.portfolio(); }
    [[nodiscard]] const std::map<OrderId, SimOrder> &orders() const { return engine_.orders(); }
    [[nodiscard]] Timestamp now() const { return engine_.now(); }
    OrderId submit_limit(Side side, PriceTicks p, QuantityLots q,
                         TimeInForce tif = TimeInForce::GTC) {
        return engine_.submit(side, OrderType::Limit, tif, q, p);
    }
    OrderId submit_market(Side side, QuantityLots q) {
        return engine_.submit(side, OrderType::Market, TimeInForce::IOC, q);
    }
    void cancel(OrderId id) { engine_.cancel(id); }
};
class Strategy {
  public:
    virtual ~Strategy() = default;
    virtual void on_start(Context &) {}
    virtual void on_market_event(Context &, const MarketEvent &) = 0;
    virtual void on_fill(Context &, const Fill &) {}
    virtual void on_timer(Context &, Timestamp) {}
    virtual void on_end(Context &) {}
};
struct ImbalanceParameters {
    std::size_t levels = 5;
    double entry_threshold = 0.35, exit_threshold = 0.10;
    QuantityLots size{1};
};
class ImbalanceStrategy final : public Strategy {
    ImbalanceParameters parameters_;

  public:
    explicit ImbalanceStrategy(ImbalanceParameters = {});
    void on_market_event(Context &, const MarketEvent &) override;
};
struct MakerParameters {
    QuantityLots size{1}, position_limit{10};
    PriceTicks min_spread{1}, max_spread{1000000}, skew_ticks_per_lot{1};
};
class MarketMaker final : public Strategy {
    MakerParameters parameters_;
    std::optional<OrderId> buy_, sell_;
    bool buy_cancel_ = false, sell_cancel_ = false;
    void quote(Context &);

  public:
    explicit MarketMaker(MakerParameters = {});
    void on_market_event(Context &, const MarketEvent &) override;
    void on_timer(Context &, Timestamp) override;
};
struct MarketSample {
    Timestamp timestamp;
    std::optional<PriceTicks> bid, ask;
    std::optional<Midpoint> mid;
    double imbalance;
    PortfolioState portfolio;
    std::optional<Valuation> valuation;
    bool operator==(const MarketSample &) const = default;
};
struct Result {
    std::vector<SimOrder> orders;
    std::vector<Fill> fills;
    std::vector<Rejection> rejections;
    std::vector<MarketSample> history;
    bool operator==(const Result &) const = default;
};
struct RunConfig {
    ExecutionConfig execution;
    RiskLimits risk;
    MoneyAtoms initial_cash = 0;
    Timestamp timer_interval;
    std::size_t signal_levels = 5;
    std::uint64_t seed = 0;
};
Result run_backtest(EventSource &, Strategy &, RunConfig = {});
} // namespace microback
#endif
