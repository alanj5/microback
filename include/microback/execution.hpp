#ifndef MICROBACK_EXECUTION_HPP
#define MICROBACK_EXECUTION_HPP
#include "microback/portfolio.hpp"
#include "microback/replay_engine.hpp"
#include "microback/risk.hpp"
#include <map>
#include <queue>
#include <vector>
namespace microback {
enum class QueueModel : std::uint8_t { Conservative, Neutral, Optimistic };
enum class Liquidity : std::uint8_t { Maker, Taker };
// One atom = one millionth of one tick*lot monetary unit.

class LatencyModel {
  public:
    virtual ~LatencyModel() = default;
    virtual Timestamp sample_latency() = 0;
};
class ConstantLatency final : public LatencyModel {
    Timestamp latency_;

  public:
    explicit ConstantLatency(Timestamp);
    Timestamp sample_latency() override { return latency_; }
};
struct ExecutionConfig {
    Timestamp latency_ns;
    QueueModel queue_model = QueueModel::Conservative;
    std::int32_t maker_fee_ppm = 0;
    std::int32_t taker_fee_ppm = 0;
};
struct SimOrder {
    OrderId client_order_id;
    Side side;
    OrderType type;
    TimeInForce tif;
    PriceTicks limit_price;
    QuantityLots initial_quantity;
    QuantityLots remaining_quantity;
    QuantityLots filled_quantity;
    QuantityLots cancelled_quantity;
    Timestamp submitted_at;
    Timestamp exchange_arrival_at;
    OrderStatus status = OrderStatus::Pending;
    std::optional<Midpoint> decision_mid;
    std::optional<Midpoint> arrival_mid;
    QuantityLots historical_ahead;
    QuantityLots client_ahead;
    bool operator==(const SimOrder &) const = default;
};
struct Fill {
    OrderId client_order_id;
    Timestamp timestamp;
    Side side;
    PriceTicks price;
    QuantityLots quantity;
    Liquidity liquidity;
    MoneyAtoms fee_atoms;
    std::optional<Midpoint> decision_mid;
    std::optional<Midpoint> arrival_mid;
    bool operator==(const Fill &) const = default;
};
class ExecutionEngine {
    enum class ActionKind : std::uint8_t { Submit, Cancel };
    struct Action {
        Timestamp time;
        Sequence sequence;
        OrderId id;
        ActionKind kind;
    };
    struct Later {
        bool operator()(const Action &a, const Action &b) const {
            return a.time != b.time ? a.time > b.time : a.sequence > b.sequence;
        }
    };
    using LevelKey = std::pair<Side, PriceTicks>;
    ReplayEngine replay_;
    ExecutionConfig config_;
    RiskLimits risk_;
    Portfolio portfolio_;
    std::vector<Rejection> rejections_;
    ConstantLatency latency_;
    Timestamp now_;
    std::uint64_t next_id_ = 1, next_action_ = 1;
    std::map<OrderId, SimOrder> orders_;
    std::vector<Fill> fills_;
    std::priority_queue<Action, std::vector<Action>, Later> actions_;
    std::map<LevelKey, std::int64_t> consumed_;
    std::map<LevelKey, std::int64_t> unreflected_trades_;
    bool ready_ = false, failed_ = false, loading_snapshot_ = false;
    [[nodiscard]] Timestamp arrival();
    void schedule(OrderId, ActionKind, Timestamp);
    void process_until(Timestamp, bool inclusive);
    void activate(SimOrder &);
    void cancel_at_exchange(SimOrder &);
    void fill(SimOrder &, PriceTicks, std::int64_t, Liquidity);
    void passive_trade(const MarketEvent &);
    struct Cancellation {
        std::int64_t removed;
        std::int64_t pool_before;
    };
    void cancellation_effect(LevelKey, Cancellation);
    [[nodiscard]] std::int64_t visible(LevelKey) const;

  public:
    explicit ExecutionEngine(ExecutionConfig = {}, RiskLimits = {}, MoneyAtoms initial_cash = 0);
    // Submit only schedules. Call flush() after accepting all requests at current time.
    OrderId submit(Side, OrderType, TimeInForce, QuantityLots, PriceTicks limit = {});
    void cancel(OrderId);
    void flush();
    bool on_market_event(const MarketEvent &);
    void run(EventSource &);
    [[nodiscard]] const OrderBook &book() const noexcept { return replay_.book(); }
    [[nodiscard]] Timestamp now() const noexcept { return now_; }
    [[nodiscard]] const std::map<OrderId, SimOrder> &orders() const noexcept { return orders_; }
    [[nodiscard]] const std::vector<Fill> &fills() const noexcept { return fills_; }
    [[nodiscard]] const Portfolio &portfolio() const noexcept { return portfolio_; }
    [[nodiscard]] const std::vector<Rejection> &rejections() const noexcept { return rejections_; }
    [[nodiscard]] bool valid() const;
};
} // namespace microback
#endif
