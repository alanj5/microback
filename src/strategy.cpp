#include "microback/strategy.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace microback {
namespace {
bool live(const SimOrder &o) { return o.remaining_quantity.value() > 0; }
Timestamp next_time(Timestamp now, Timestamp interval) {
    const auto value = static_cast<MoneyAtoms>(now.value()) + interval.value();
    if (value > std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("timer overflow");
    return Timestamp{static_cast<std::int64_t>(value)};
}
} // namespace
ImbalanceStrategy::ImbalanceStrategy(ImbalanceParameters p) : parameters_(p) {
    if (p.levels == 0 || !std::isfinite(p.entry_threshold) || !std::isfinite(p.exit_threshold) ||
        p.entry_threshold <= 0 || p.entry_threshold > 1 || p.exit_threshold < 0 ||
        p.exit_threshold >= p.entry_threshold || p.size.value() <= 0)
        throw std::invalid_argument("invalid imbalance parameters");
}
void ImbalanceStrategy::on_market_event(Context &ctx, const MarketEvent &) {
    if (!ctx.book().midpoint())
        return;
    for (const auto &[id, o] : ctx.orders()) {
        (void)id;
        if (live(o))
            return;
    }
    const auto signal = ctx.book().imbalance(parameters_.levels);
    const auto current = ctx.portfolio().state().position.value();
    auto target = current;
    if (signal >= parameters_.entry_threshold)
        target = parameters_.size.value();
    else if (signal <= -parameters_.entry_threshold)
        target = -parameters_.size.value();
    else if (std::abs(signal) <= parameters_.exit_threshold)
        target = 0;
    const auto delta = static_cast<MoneyAtoms>(target) - current;
    if (delta == 0)
        return;
    if (delta > std::numeric_limits<std::int64_t>::max() ||
        delta < -static_cast<MoneyAtoms>(std::numeric_limits<std::int64_t>::max()))
        throw std::overflow_error("strategy size overflow");
    ctx.submit_market(delta > 0 ? Side::Buy : Side::Sell,
                      QuantityLots{static_cast<std::int64_t>(delta > 0 ? delta : -delta)});
}
MarketMaker::MarketMaker(MakerParameters p) : parameters_(p) {
    if (p.size.value() <= 0 || p.position_limit.value() <= 0 || p.min_spread.value() < 1 ||
        p.max_spread < p.min_spread || p.skew_ticks_per_lot.value() < 0)
        throw std::invalid_argument("invalid maker parameters");
}
void MarketMaker::quote(Context &ctx) {
    const auto bid = ctx.book().best_bid(), ask = ctx.book().best_ask();
    const auto pos = ctx.portfolio().state().position.value();
    const auto skew = static_cast<MoneyAtoms>(pos) * parameters_.skew_ticks_per_lot.value();
    auto update = [&](Side side, std::optional<OrderId> &id, bool &cancelling) {
        bool allowed = bid && ask;
        if (allowed) {
            const auto spread = ask->value() - bid->value();
            allowed = spread >= parameters_.min_spread.value() &&
                      spread <= parameters_.max_spread.value();
        }
        const auto projected =
            static_cast<MoneyAtoms>(pos) +
            (side == Side::Buy ? parameters_.size.value() : -parameters_.size.value());
        allowed = allowed && projected <= parameters_.position_limit.value() &&
                  projected >= -static_cast<MoneyAtoms>(parameters_.position_limit.value());
        MoneyAtoms price = 0;
        if (allowed)
            price = side == Side::Buy
                        ? std::min<MoneyAtoms>(ask->value() - 1,
                                               static_cast<MoneyAtoms>(bid->value()) - skew)
                        : std::max<MoneyAtoms>(bid->value() + static_cast<MoneyAtoms>(1),
                                               static_cast<MoneyAtoms>(ask->value()) - skew);
        allowed = allowed && price > 0 && price <= std::numeric_limits<std::int64_t>::max();
        if (id) {
            const auto &old = ctx.orders().at(*id);
            if (live(old)) {
                if ((!allowed || old.limit_price.value() != price ||
                     old.remaining_quantity != parameters_.size) &&
                    !cancelling) {
                    ctx.cancel(*id);
                    cancelling = true;
                }
                return;
            }
            id.reset();
            cancelling = false;
        }
        if (allowed)
            id = ctx.submit_limit(side, PriceTicks{static_cast<std::int64_t>(price)},
                                  parameters_.size);
    };
    update(Side::Buy, buy_, buy_cancel_);
    update(Side::Sell, sell_, sell_cancel_);
}
void MarketMaker::on_market_event(Context &ctx, const MarketEvent &) { quote(ctx); }
void MarketMaker::on_timer(Context &ctx, Timestamp) { quote(ctx); }
Result run_backtest(EventSource &source, Strategy &strategy, RunConfig config) {
    if (config.timer_interval.value() < 0 || config.signal_levels == 0)
        throw std::invalid_argument("invalid run configuration");
    ExecutionEngine engine{config.execution, config.risk, config.initial_cash};
    Context ctx{engine};
    Result result;
    auto sample = [&] {
        const auto mid = engine.book().midpoint();
        result.history.push_back(
            {engine.now(), engine.book().best_bid(), engine.book().best_ask(), mid,
             engine.book().imbalance(config.signal_levels), engine.portfolio().state(),
             mid ? std::optional<Valuation>{engine.portfolio().mark(*mid)} : std::nullopt});
    };
    engine.set_fill_callback([&](const Fill &fill) {
        strategy.on_fill(ctx, fill);
        sample();
    });
    bool started = false;
    Timestamp timer;
    MarketEvent event{};
    while (source.next(event)) {
        if (started && config.timer_interval.value() > 0) {
            while (timer < event.timestamp_ns) {
                engine.advance_to(timer);
                strategy.on_timer(ctx, timer);
                engine.flush();
                sample();
                timer = next_time(timer, config.timer_interval);
            }
        }
        if (!engine.on_market_event(event))
            continue;
        if (!started) {
            started = true;
            strategy.on_start(ctx);
            timer = config.timer_interval.value() > 0
                        ? next_time(engine.now(), config.timer_interval)
                        : Timestamp{};
        }
        strategy.on_market_event(ctx, event);
        engine.flush();
        if (config.timer_interval.value() > 0 && timer == engine.now()) {
            strategy.on_timer(ctx, timer);
            engine.flush();
            timer = next_time(timer, config.timer_interval);
        }
        sample();
    }
    engine.finish();
    if (started) {
        strategy.on_end(ctx);
        engine.flush();
        sample();
    }
    for (const auto &[id, order] : engine.orders()) {
        (void)id;
        result.orders.push_back(order);
    }
    result.fills = engine.fills();
    result.rejections = engine.rejections();
    return result;
}
} // namespace microback
