#include "microback/binary.hpp"
#include "microback/strategy.hpp"
#include <algorithm>
#include <limits>
#include <memory>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
namespace py = pybind11;
using namespace microback;
namespace {
py::int_ integer(MoneyAtoms value) {
    const bool negative = value < 0;
    auto magnitude = negative ? static_cast<unsigned __int128>(-(value + 1)) + 1
                              : static_cast<unsigned __int128>(value);
    std::string s;
    do {
        s.push_back(static_cast<char>('0' + magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);
    if (negative)
        s.push_back('-');
    std::ranges::reverse(s);
    return py::module_::import("builtins").attr("int")(s).cast<py::int_>();
}
MoneyAtoms atoms(const py::handle &value) {
    const auto s = py::str(value).cast<std::string>();
    if (s.empty())
        throw py::value_error("empty monetary integer");
    const bool negative = s[0] == '-';
    MoneyAtoms result = 0;
    for (std::size_t i = negative ? 1 : 0; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9')
            throw py::value_error("money requires an integer");
        result = money_add(money_mul(result, 10), negative ? -(s[i] - '0') : s[i] - '0');
    }
    return result;
}
template <class T> void setting(const py::dict &d, const char *key, T &value) {
    if (d.contains(key))
        value = d[key].cast<T>();
}
py::object midpoint(const std::optional<Midpoint> &m) {
    return m ? py::cast(m->as_double()) : py::none();
}
py::dict run(const std::string &path, const py::dict &p, const std::string &name,
             const py::dict &c) {
    RunConfig cfg;
    std::int64_t latency = 0, timer = 0, position = std::numeric_limits<std::int64_t>::max(),
                 order_size = position;
    setting(c, "latency_ns", latency);
    setting(c, "timer_interval_ns", timer);
    setting(c, "max_position", position);
    setting(c, "max_order_size", order_size);
    cfg.execution.latency_ns = Timestamp{latency};
    cfg.timer_interval = Timestamp{timer};
    cfg.risk.max_position = QuantityLots{position};
    cfg.risk.max_order_size = QuantityLots{order_size};
    setting(c, "maker_fee_ppm", cfg.execution.maker_fee_ppm);
    setting(c, "taker_fee_ppm", cfg.execution.taker_fee_ppm);
    setting(c, "signal_levels", cfg.signal_levels);
    setting(c, "seed", cfg.seed);
    if (c.contains("initial_cash_atoms"))
        cfg.initial_cash = atoms(c["initial_cash_atoms"]);
    if (c.contains("max_notional_atoms"))
        cfg.risk.max_notional_atoms = atoms(c["max_notional_atoms"]);
    std::string queue = "conservative";
    setting(c, "queue_model", queue);
    if (queue == "neutral")
        cfg.execution.queue_model = QueueModel::Neutral;
    else if (queue == "optimistic")
        cfg.execution.queue_model = QueueModel::Optimistic;
    else if (queue != "conservative")
        throw py::value_error("unknown queue model");
    std::unique_ptr<Strategy> strategy;
    std::int64_t size = 1;
    setting(p, "size", size);
    if (name == "imbalance") {
        ImbalanceParameters ip;
        ip.size = QuantityLots{size};
        setting(p, "levels", ip.levels);
        setting(p, "entry_threshold", ip.entry_threshold);
        setting(p, "exit_threshold", ip.exit_threshold);
        strategy = std::make_unique<ImbalanceStrategy>(ip);
    } else if (name == "market_maker") {
        MakerParameters mp;
        std::int64_t limit = 10, min = 1, max = 1000000, skew = 1;
        setting(p, "position_limit", limit);
        setting(p, "min_spread", min);
        setting(p, "max_spread", max);
        setting(p, "skew_ticks_per_lot", skew);
        mp.size = QuantityLots{size};
        mp.position_limit = QuantityLots{limit};
        mp.min_spread = PriceTicks{min};
        mp.max_spread = PriceTicks{max};
        mp.skew_ticks_per_lot = PriceTicks{skew};
        strategy = std::make_unique<MarketMaker>(mp);
    } else
        throw py::value_error("unknown strategy");
    auto source = open_event_source(path);
    Result result;
    {
        py::gil_scoped_release release;
        result = run_backtest(*source, *strategy, cfg);
    }
    py::list orders, fills, history, rejections;
    const char *statuses[] = {"pending", "active",    "partially_filled",
                              "filled",  "cancelled", "rejected"};
    for (const auto &o : result.orders) {
        py::dict d;
        d["client_order_id"] = o.client_order_id.value();
        d["side"] = o.side == Side::Buy ? "buy" : "sell";
        d["type"] = o.type == OrderType::Market ? "market" : "limit";
        d["tif"] = o.tif == TimeInForce::GTC ? "GTC" : "IOC";
        d["limit_price_ticks"] = o.limit_price.value();
        d["quantity_lots"] = o.initial_quantity.value();
        d["remaining_lots"] = o.remaining_quantity.value();
        d["filled_lots"] = o.filled_quantity.value();
        d["cancelled_lots"] = o.cancelled_quantity.value();
        d["submitted_ns"] = o.submitted_at.value();
        d["arrival_ns"] = o.exchange_arrival_at.value();
        d["status"] = statuses[static_cast<std::size_t>(o.status)];
        orders.append(d);
    }
    for (const auto &f : result.fills) {
        py::dict d;
        d["client_order_id"] = f.client_order_id.value();
        d["timestamp_ns"] = f.timestamp.value();
        d["side"] = f.side == Side::Buy ? "buy" : "sell";
        d["price_ticks"] = f.price.value();
        d["quantity_lots"] = f.quantity.value();
        d["liquidity"] = f.liquidity == Liquidity::Maker ? "maker" : "taker";
        d["fee_atoms"] = integer(f.fee_atoms);
        d["decision_mid_ticks"] = midpoint(f.decision_mid);
        d["arrival_mid_ticks"] = midpoint(f.arrival_mid);
        fills.append(d);
    }
    for (const auto &h : result.history) {
        py::dict d;
        d["timestamp_ns"] = h.timestamp.value();
        d["bid_ticks"] = h.bid ? py::cast(h.bid->value()) : py::none();
        d["ask_ticks"] = h.ask ? py::cast(h.ask->value()) : py::none();
        d["mid_ticks"] = midpoint(h.mid);
        d["imbalance"] = h.imbalance;
        d["position_lots"] = h.portfolio.position.value();
        d["cash_atoms"] = integer(h.portfolio.cash);
        d["realised_pnl_atoms"] = integer(h.portfolio.realised_pnl);
        d["fees_atoms"] = integer(h.portfolio.fees);
        d["turnover_atoms"] = integer(h.portfolio.turnover);
        d["decision_slippage_atoms"] = integer(h.portfolio.decision_slippage);
        d["arrival_slippage_atoms"] = integer(h.portfolio.arrival_slippage);
        d["equity_atoms"] = h.valuation ? py::object(integer(h.valuation->equity)) : py::none();
        d["gross_pnl_atoms"] =
            h.valuation ? py::object(integer(h.valuation->gross_pnl)) : py::none();
        d["net_pnl_atoms"] = h.valuation ? py::object(integer(h.valuation->net_pnl)) : py::none();
        d["unrealised_pnl_atoms"] =
            h.valuation ? py::object(integer(h.valuation->unrealised_pnl)) : py::none();
        d["exposure_atoms"] = h.valuation ? py::object(integer(h.valuation->exposure)) : py::none();
        history.append(d);
    }
    for (const auto &r : result.rejections) {
        py::dict d;
        d["client_order_id"] = r.id.value();
        d["timestamp_ns"] = r.timestamp.value();
        d["reason"] = static_cast<unsigned>(r.reason);
        rejections.append(d);
    }
    py::dict out;
    out["orders"] = orders;
    out["fills"] = fills;
    out["history"] = history;
    out["rejections"] = rejections;
    return out;
}
} // namespace
PYBIND11_MODULE(_core, m) {
    m.def("run_native", &run, py::arg("path"), py::arg("parameters"), py::arg("strategy"),
          py::arg("config"));
}
