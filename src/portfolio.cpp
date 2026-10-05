#include "microback/portfolio.hpp"
#include "microback/execution.hpp"
#include <algorithm>
#include <limits>
namespace microback {
namespace {
MoneyAtoms abs_money(MoneyAtoms x) { return x < 0 ? money_mul(x, -1) : x; }
MoneyAtoms mark_value(Midpoint mark, MoneyAtoms qty) {
    const auto price = money_add(money_mul(mark.floor_ticks.value(), 2), mark.half_tick ? 1 : 0);
    return money_mul(money_mul(price, qty), money_scale) / 2;
}
} // namespace
Portfolio::Portfolio(MoneyAtoms initial_cash) : initial_cash_(initial_cash) {
    state_.cash = initial_cash;
}
void Portfolio::apply(const Fill &fill) {
    if (fill.quantity.value() <= 0 || fill.price.value() <= 0 ||
        (fill.side != Side::Buy && fill.side != Side::Sell))
        throw std::invalid_argument("invalid accounting fill");
    auto next = state_;
    const MoneyAtoms old = next.position.value();
    const MoneyAtoms delta =
        fill.side == Side::Buy ? fill.quantity.value() : -fill.quantity.value();
    const auto position = money_add(old, delta);
    if (position < std::numeric_limits<std::int64_t>::min() ||
        position > std::numeric_limits<std::int64_t>::max())
        throw std::overflow_error("position overflow");
    const auto notional = notional_atoms(fill.price, fill.quantity);
    next.cash = money_add(next.cash, fill.side == Side::Buy ? -notional : notional);
    next.cash = money_add(next.cash, -fill.fee_atoms);
    next.fees = money_add(next.fees, fill.fee_atoms);
    next.turnover = money_add(next.turnover, notional);
    if (old == 0 || (old > 0) == (delta > 0))
        next.basis = money_add(next.basis, notional);
    else {
        const auto old_size = abs_money(old), close = std::min(old_size, abs_money(delta));
        // Divide first: allocation products are bounded by basis; remainder product fits 128 bits.
        const auto allocation = money_add(money_mul(next.basis / old_size, close),
                                          money_mul(next.basis % old_size, close) / old_size);
        const auto closed_value =
            notional_atoms(fill.price, QuantityLots{static_cast<std::int64_t>(close)});
        next.realised_pnl = money_add(next.realised_pnl, old > 0 ? closed_value - allocation
                                                                 : allocation - closed_value);
        next.basis -= allocation;
        const auto opening = abs_money(delta) - close;
        if (opening > 0)
            next.basis =
                notional_atoms(fill.price, QuantityLots{static_cast<std::int64_t>(opening)});
    }
    next.position = QuantityLots{static_cast<std::int64_t>(position)};
    const auto signed_cost = fill.side == Side::Buy ? notional : -notional;
    if (fill.decision_mid)
        next.decision_slippage =
            money_add(next.decision_slippage, signed_cost - mark_value(*fill.decision_mid, delta));
    if (fill.arrival_mid)
        next.arrival_slippage =
            money_add(next.arrival_slippage, signed_cost - mark_value(*fill.arrival_mid, delta));
    state_ = next;
}
Valuation Portfolio::mark(Midpoint mid) const {
    const auto value = mark_value(mid, state_.position.value());
    const auto equity = money_add(state_.cash, value);
    const auto net = money_add(equity, -initial_cash_);
    const auto gross = money_add(net, state_.fees);
    const auto signed_basis = state_.position.value() < 0 ? -state_.basis : state_.basis;
    return {equity, gross, net, value - signed_basis, abs_money(value)};
}
double Portfolio::average_entry_ticks() const {
    if (state_.position.value() == 0)
        return 0;
    return static_cast<double>(state_.basis) /
           static_cast<double>(abs_money(state_.position.value())) / 1000000.0;
}
} // namespace microback
