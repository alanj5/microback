#include "microback/execution.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace microback {
namespace {
bool terminal(OrderStatus status) {
    return status == OrderStatus::Filled || status == OrderStatus::Cancelled ||
           status == OrderStatus::Rejected;
}
std::int64_t narrow(MoneyAtoms value) {
    if (value < 0 || value > std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("execution integer overflow");
    return static_cast<std::int64_t>(value);
}
} // namespace
ConstantLatency::ConstantLatency(Timestamp latency) : latency_(latency) {
    if (latency.value() < 0)
        throw std::invalid_argument("negative latency");
}
ExecutionEngine::ExecutionEngine(ExecutionConfig config, RiskLimits risk, MoneyAtoms initial_cash)
    : config_(config), risk_(risk), portfolio_(initial_cash), latency_(config.latency_ns) {
    validate_limits(risk_);
    if (config.queue_model != QueueModel::Conservative &&
        config.queue_model != QueueModel::Neutral && config.queue_model != QueueModel::Optimistic)
        throw std::invalid_argument("invalid queue model");
    if (config.maker_fee_ppm < -1000000 || config.maker_fee_ppm > 1000000 ||
        config.taker_fee_ppm < -1000000 || config.taker_fee_ppm > 1000000)
        throw std::invalid_argument("fee rate outside +/-100 percent");
}
Timestamp ExecutionEngine::arrival() {
    return Timestamp{
        narrow(static_cast<MoneyAtoms>(now_.value()) + latency_.sample_latency().value())};
}
void ExecutionEngine::schedule(OrderId id, ActionKind kind, Timestamp time) {
    if (next_action_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("action sequence exhausted");
    actions_.push({time, Sequence{next_action_++}, id, kind});
}
OrderId ExecutionEngine::submit(Side side, OrderType type, TimeInForce tif, QuantityLots quantity,
                                PriceTicks limit) {
    if (failed_)
        throw std::logic_error("execution failed");
    if (!ready_ || loading_snapshot_)
        throw std::logic_error("submit before complete snapshot");
    if ((side != Side::Buy && side != Side::Sell) ||
        (type != OrderType::Market && type != OrderType::Limit) ||
        (tif != TimeInForce::GTC && tif != TimeInForce::IOC) || quantity.value() <= 0 ||
        (type == OrderType::Limit ? limit.value() <= 0 : limit.value() != 0))
        throw std::invalid_argument("invalid client order");
    if (next_id_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("client IDs exhausted");
    const OrderId id{next_id_};
    const auto time = arrival();
    SimOrder order{};
    order.client_order_id = id;
    order.side = side;
    order.type = type;
    order.tif = tif;
    order.limit_price = limit;
    order.initial_quantity = quantity;
    order.remaining_quantity = quantity;
    order.submitted_at = now_;
    order.exchange_arrival_at = time;
    order.decision_mid = book().midpoint();
    std::optional<RejectionReason> reason;
    if (quantity > risk_.max_order_size)
        reason = RejectionReason::OrderSize;
    MoneyAtoms buys = 0, sells = 0;
    for (const auto &[existing_id, existing] : orders_) {
        (void)existing_id;
        if (existing.side == Side::Buy)
            buys += existing.remaining_quantity.value();
        else
            sells += existing.remaining_quantity.value();
    }
    if (side == Side::Buy)
        buys += quantity.value();
    else
        sells += quantity.value();
    const auto position = portfolio_.state().position.value();
    if (!reason && (position + buys > risk_.max_position.value() ||
                    position - sells < -static_cast<MoneyAtoms>(risk_.max_position.value())))
        reason = RejectionReason::Position;
    if (!reason && risk_.max_notional_atoms > 0) {
        auto reference = type == OrderType::Limit
                             ? std::optional<PriceTicks>{limit}
                             : (side == Side::Buy ? book().best_ask() : book().best_bid());
        if (type == OrderType::Market) {
            const auto depth = book().depth(side == Side::Buy ? Side::Sell : Side::Buy,
                                            std::numeric_limits<std::size_t>::max());
            if (!depth.empty())
                reference = depth.back().price;
        }
        if (!reference)
            reason = RejectionReason::MissingPrice;
        else if (notional_atoms(*reference, quantity) > risk_.max_notional_atoms)
            reason = RejectionReason::Notional;
    }
    if (reason) {
        order.status = OrderStatus::Rejected;
        order.remaining_quantity = QuantityLots{0};
        order.cancelled_quantity = quantity;
        orders_.emplace(id, order);
        rejections_.push_back({id, now_, *reason});
        ++next_id_;
        return id;
    }
    orders_.emplace(id, order);
    try {
        schedule(id, ActionKind::Submit, time);
    } catch (...) {
        orders_.erase(id);
        throw;
    }
    ++next_id_;
    return id;
}
void ExecutionEngine::cancel(OrderId id) {
    if (!ready_ || loading_snapshot_)
        throw std::logic_error("cancel during incomplete snapshot");
    if (failed_)
        throw std::logic_error("execution failed");
    if (!orders_.contains(id))
        throw std::invalid_argument("unknown client ID");
    schedule(id, ActionKind::Cancel, arrival());
}
std::int64_t ExecutionEngine::visible(LevelKey key) const {
    return book().quantity_at(key.first, key.second).value();
}
void ExecutionEngine::fill(SimOrder &order, PriceTicks price, std::int64_t quantity,
                           Liquidity liquidity) {
    const auto notional = narrow(static_cast<MoneyAtoms>(price.value()) * quantity);
    const auto ppm = liquidity == Liquidity::Maker ? config_.maker_fee_ppm : config_.taker_fee_ppm;
    Fill record{order.client_order_id,
                now_,
                order.side,
                price,
                QuantityLots{quantity},
                liquidity,
                static_cast<MoneyAtoms>(notional) * ppm,
                order.decision_mid,
                order.arrival_mid};
    auto next_portfolio = portfolio_;
    next_portfolio.apply(record);
    fills_.push_back(record);
    portfolio_ = next_portfolio;
    order.remaining_quantity = QuantityLots{order.remaining_quantity.value() - quantity};
    order.filled_quantity = QuantityLots{order.filled_quantity.value() + quantity};
    order.status =
        order.remaining_quantity.value() == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    if (fill_callback_)
        fill_callback_(record);
}
void ExecutionEngine::cancel_at_exchange(SimOrder &order) {
    if (terminal(order.status))
        return;
    if (order.status != OrderStatus::Pending) {
        for (auto &[id, later] : orders_) {
            if (id > order.client_order_id && later.side == order.side &&
                later.limit_price == order.limit_price && later.status != OrderStatus::Pending &&
                !terminal(later.status)) {
                later.client_ahead = QuantityLots{std::max<std::int64_t>(
                    0, later.client_ahead.value() - order.remaining_quantity.value())};
            }
        }
    }
    order.cancelled_quantity =
        QuantityLots{order.cancelled_quantity.value() + order.remaining_quantity.value()};
    order.remaining_quantity = QuantityLots{0};
    order.status = OrderStatus::Cancelled;
}
void ExecutionEngine::activate(SimOrder &order) {
    if (order.status != OrderStatus::Pending)
        return;
    order.arrival_mid = book().midpoint();
    order.status = OrderStatus::Active;
    const auto side = order.side == Side::Buy ? Side::Sell : Side::Buy;
    for (const auto &level : book().depth(side, std::numeric_limits<std::size_t>::max())) {
        if (order.remaining_quantity.value() == 0)
            break;
        if (order.type == OrderType::Limit &&
            (order.side == Side::Buy ? level.price > order.limit_price
                                     : level.price < order.limit_price))
            break;
        auto &consumed = consumed_[{side, level.price}];
        const auto available = narrow(
            std::max<MoneyAtoms>(0, static_cast<MoneyAtoms>(level.quantity.value()) - consumed -
                                        unreflected_trades_[{side, level.price}]));
        const auto take = std::min(available, order.remaining_quantity.value());
        if (take > 0) {
            fill(order, level.price, take, Liquidity::Taker);
            consumed += take;
        }
    }
    if (order.remaining_quantity.value() == 0)
        return;
    if (order.type == OrderType::Market || order.tif == TimeInForce::IOC) {
        cancel_at_exchange(order);
        return;
    }
    const LevelKey own_level{order.side, order.limit_price};
    order.historical_ahead = QuantityLots{
        narrow(std::max<MoneyAtoms>(0, static_cast<MoneyAtoms>(visible(own_level)) -
                                           consumed_[own_level] - unreflected_trades_[own_level]))};
    MoneyAtoms client = 0;
    for (const auto &[id, earlier] : orders_) {
        if (id == order.client_order_id)
            break;
        if (earlier.status != OrderStatus::Pending && !terminal(earlier.status) &&
            earlier.side == order.side && earlier.limit_price == order.limit_price)
            client += earlier.remaining_quantity.value();
    }
    order.client_ahead = QuantityLots{narrow(client)};
}
void ExecutionEngine::process_until(Timestamp time, bool inclusive) {
    while (!actions_.empty() &&
           (actions_.top().time < time || (inclusive && actions_.top().time == time))) {
        const auto action = actions_.top();
        actions_.pop();
        now_ = action.time;
        auto &order = orders_.at(action.id);
        if (action.kind == ActionKind::Submit)
            activate(order);
        else
            cancel_at_exchange(order);
    }
}
void ExecutionEngine::flush() {
    if (loading_snapshot_)
        throw std::logic_error("flush during incomplete snapshot");
    if (failed_)
        throw std::logic_error("execution failed");
    const auto time = now_;
    try {
        process_until(time, true);
        now_ = time;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ExecutionEngine::passive_trade(const MarketEvent &event) {
    for (auto &[id, order] : orders_) {
        (void)id;
        if (order.status == OrderStatus::Pending || terminal(order.status) ||
            order.side != event.side || order.limit_price != event.price)
            continue;
        auto quantity = event.quantity.value();
        const auto ahead = std::min(quantity, order.historical_ahead.value());
        order.historical_ahead = QuantityLots{order.historical_ahead.value() - ahead};
        quantity -= ahead;
        const auto client = std::min(quantity, order.client_ahead.value());
        order.client_ahead = QuantityLots{order.client_ahead.value() - client};
        quantity -= client;
        if (quantity > 0)
            fill(order, event.price, std::min(quantity, order.remaining_quantity.value()),
                 Liquidity::Maker);
    }
    if (event.order_id.value() == 0) {
        auto &pending = unreflected_trades_[{event.side, event.price}];
        pending = narrow(static_cast<MoneyAtoms>(pending) + event.quantity.value());
    }
}
void ExecutionEngine::cancellation_effect(LevelKey key, Cancellation observation) {
    auto removed = observation.removed;
    const auto before = observation.pool_before;
    auto &trades = unreflected_trades_[key];
    const auto reflected = std::min(trades, removed);
    trades -= reflected;
    removed -= reflected;
    if (removed == 0 || config_.queue_model == QueueModel::Conservative)
        return;
    for (auto &[id, order] : orders_) {
        (void)id;
        if (order.side != key.first || order.limit_price != key.second ||
            order.status == OrderStatus::Pending || terminal(order.status))
            continue;
        auto reduction = removed;
        if (config_.queue_model == QueueModel::Neutral) {
            const auto pool = before - reflected;
            reduction = pool <= 0 ? 0
                                  : narrow(static_cast<MoneyAtoms>(order.historical_ahead.value()) *
                                           removed / pool);
        }
        order.historical_ahead = QuantityLots{order.historical_ahead.value() -
                                              std::min(reduction, order.historical_ahead.value())};
    }
}
bool ExecutionEngine::on_market_event(const MarketEvent &event) {
    if (failed_)
        throw std::logic_error("execution failed");
    try {
        if (event.timestamp_ns < now_)
            throw std::invalid_argument("event before execution clock");
        process_until(event.timestamp_ns, false);
        now_ = event.timestamp_ns;
        std::optional<Order> old;
        if (event.order_id.value() != 0)
            old = book().find(event.order_id);
        const bool published = replay_.ingest(event);
        if (!published) {
            loading_snapshot_ = true;
            return false;
        }
        if (event.type == MarketEventType::Snapshot) {
            loading_snapshot_ = false;
            if (ready_)
                for (auto &[id, order] : orders_) {
                    (void)id;
                    if (order.status != OrderStatus::Pending)
                        cancel_at_exchange(order);
                }
            consumed_.clear();
            unreflected_trades_.clear();
            ready_ = true;
        } else {
            if (event.type == MarketEventType::Trade)
                passive_trade(event);
            if (old &&
                (event.type == MarketEventType::Cancel || event.type == MarketEventType::Modify)) {
                const auto after =
                    event.type == MarketEventType::Cancel || event.price != old->price
                        ? 0
                        : event.quantity.value();
                const LevelKey key{old->side, old->price};
                const auto total_before = visible(key) - after + old->quantity.value();
                if (after < old->quantity.value())
                    cancellation_effect(key, {.removed = old->quantity.value() - after,
                                              .pool_before = total_before});
            }
            // Shadow consumption is capped by current historical depth. Growth adds
            // available lots once; it does not also release previously consumed lots.
            if (old) {
                const LevelKey key{old->side, old->price};
                auto &used = consumed_[key];
                used = std::min(used, visible(key));
            }
        }
        process_until(event.timestamp_ns, true);
        now_ = event.timestamp_ns;
        return true;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ExecutionEngine::finish() {
    if (failed_)
        throw std::logic_error("execution failed");
    try {
        replay_.finish();
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ExecutionEngine::advance_to(Timestamp time) {
    if (failed_ || loading_snapshot_)
        throw std::logic_error("cannot advance failed/incomplete engine");
    if (time < now_)
        throw std::invalid_argument("clock regression");
    try {
        process_until(time, true);
        now_ = time;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ExecutionEngine::run(EventSource &source) {
    if (failed_)
        throw std::logic_error("execution failed");
    try {
        MarketEvent event{};
        while (source.next(event))
            on_market_event(event);
        replay_.finish();
    } catch (...) {
        failed_ = true;
        throw;
    }
}
bool ExecutionEngine::valid() const {
    if (!book().valid())
        return false;
    for (const auto &[id, order] : orders_) {
        if (id != order.client_order_id || order.initial_quantity.value() <= 0 ||
            order.remaining_quantity.value() < 0 || order.filled_quantity.value() < 0 ||
            order.cancelled_quantity.value() < 0 ||
            static_cast<MoneyAtoms>(order.remaining_quantity.value()) +
                    order.filled_quantity.value() + order.cancelled_quantity.value() !=
                order.initial_quantity.value() ||
            order.historical_ahead.value() < 0 || order.client_ahead.value() < 0 ||
            order.exchange_arrival_at < order.submitted_at)
            return false;
        if (terminal(order.status) != (order.remaining_quantity.value() == 0))
            return false;
        if (order.status == OrderStatus::Filled && order.filled_quantity != order.initial_quantity)
            return false;
    }
    return true;
}
} // namespace microback
