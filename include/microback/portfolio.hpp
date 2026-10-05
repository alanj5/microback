#ifndef MICROBACK_PORTFOLIO_HPP
#define MICROBACK_PORTFOLIO_HPP
#include "microback/money.hpp"
#include "microback/order_book.hpp"
namespace microback {
struct Fill;
struct PortfolioState {
    MoneyAtoms cash = 0, basis = 0, realised_pnl = 0, fees = 0, turnover = 0;
    QuantityLots position;
    MoneyAtoms decision_slippage = 0, arrival_slippage = 0;
    bool operator==(const PortfolioState &) const = default;
};
struct Valuation {
    MoneyAtoms equity, gross_pnl, net_pnl, unrealised_pnl, exposure;
    bool operator==(const Valuation &) const = default;
};
class Portfolio {
    MoneyAtoms initial_cash_;
    PortfolioState state_;

  public:
    explicit Portfolio(MoneyAtoms initial_cash = 0);
    void apply(const Fill &);
    [[nodiscard]] const PortfolioState &state() const noexcept { return state_; }
    [[nodiscard]] Valuation mark(Midpoint) const;
    [[nodiscard]] double average_entry_ticks() const;
};
} // namespace microback
#endif
