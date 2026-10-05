#include "microback/order_book.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace microback {
namespace {
void check_side(Side side) {
    if (side != Side::Buy && side != Side::Sell)
        throw std::invalid_argument("invalid side");
}
void check_values(PriceTicks price, QuantityLots quantity) {
    if (price.value() <= 0 || quantity.value() <= 0)
        throw std::invalid_argument("price and quantity must be positive");
}
std::int64_t checked_total(std::int64_t base, std::int64_t addition) {
    if (addition > std::numeric_limits<std::int64_t>::max() - base)
        throw std::overflow_error("level quantity overflow");
    return base + addition;
}
// GCC/Clang 128-bit intermediate avoids overflow in weighted integer averages.
using Wide = __int128;
} // namespace

template <class Levels> void OrderBook::insert(Levels &levels, const AddOrder &order) {
    auto [level_it, new_level] = levels.try_emplace(order.price);
    auto &level = level_it->second;
    try {
        const auto total = checked_total(level.total_quantity.value(), order.quantity.value());
        level.orders.push_back(order);
        try {
            index_.emplace(order.id, OrderLocation{&level, std::prev(level.orders.end())});
        } catch (...) {
            level.orders.pop_back();
            throw;
        }
        level.total_quantity = QuantityLots{total};
    } catch (...) {
        if (new_level)
            levels.erase(level_it);
        throw;
    }
}

void OrderBook::add(const AddOrder &order) {
    check_side(order.side);
    check_values(order.price, order.quantity);
    if (order.id.value() == 0)
        throw std::invalid_argument("zero order ID is reserved");
    if (index_.contains(order.id))
        throw std::invalid_argument("duplicate order ID");
    if (order.side == Side::Buy)
        insert(bids_, order);
    else
        insert(asks_, order);
}

void OrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end())
        throw std::invalid_argument("unknown order ID");
    auto &location = it->second;
    const auto order = *location.order;
    auto &level = *location.level;
    level.total_quantity = QuantityLots{level.total_quantity.value() - order.quantity.value()};
    level.orders.erase(location.order);
    index_.erase(it);
    if (level.orders.empty()) {
        if (order.side == Side::Buy)
            bids_.erase(order.price);
        else
            asks_.erase(order.price);
    }
}

void OrderBook::modify(const ModifyOrder &change) {
    check_values(change.price, change.quantity);
    auto it = index_.find(change.id);
    if (it == index_.end())
        throw std::invalid_argument("unknown order ID");
    auto &location = it->second;
    auto &order = *location.order;
    auto &old_level = *location.level;
    if (order.price == change.price) {
        const auto total = checked_total(old_level.total_quantity.value() - order.quantity.value(),
                                         change.quantity.value());
        const bool increase = change.quantity > order.quantity;
        order.quantity = change.quantity;
        old_level.total_quantity = QuantityLots{total};
        if (increase)
            old_level.orders.splice(old_level.orders.end(), old_level.orders, location.order);
        return;
    }
    // Allocate destination before modifying source; splice preserves identity and iterators.
    auto move = [&](auto &levels) {
        auto [dest_it, created] = levels.try_emplace(change.price);
        auto &dest = dest_it->second;
        std::int64_t total;
        try {
            total = checked_total(dest.total_quantity.value(), change.quantity.value());
        } catch (...) {
            if (created)
                levels.erase(dest_it);
            throw;
        }
        const auto old_price = order.price;
        old_level.total_quantity =
            QuantityLots{old_level.total_quantity.value() - order.quantity.value()};
        order.price = change.price;
        order.quantity = change.quantity;
        dest.orders.splice(dest.orders.end(), old_level.orders, location.order);
        dest.total_quantity = QuantityLots{total};
        location.level = &dest;
        if (old_level.orders.empty())
            levels.erase(old_price);
    };
    if (order.side == Side::Buy)
        move(bids_);
    else
        move(asks_);
}

void OrderBook::clear() noexcept {
    index_.clear();
    bids_.clear();
    asks_.clear();
}
std::optional<Order> OrderBook::find(OrderId id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? std::nullopt : std::optional<Order>{*it->second.order};
}
const PriceLevel *OrderBook::best_level(Side side) const {
    check_side(side);
    if (side == Side::Buy)
        return bids_.empty() ? nullptr : &bids_.begin()->second;
    return asks_.empty() ? nullptr : &asks_.begin()->second;
}
std::optional<PriceTicks> OrderBook::best_bid() const {
    return bids_.empty() ? std::nullopt : std::optional<PriceTicks>{bids_.begin()->first};
}
std::optional<PriceTicks> OrderBook::best_ask() const {
    return asks_.empty() ? std::nullopt : std::optional<PriceTicks>{asks_.begin()->first};
}
std::optional<PriceTicks> OrderBook::spread_ticks() const {
    if (!best_bid() || !best_ask())
        return std::nullopt;
    return PriceTicks{asks_.begin()->first.value() - bids_.begin()->first.value()};
}
std::optional<Midpoint> OrderBook::midpoint() const {
    if (!best_bid() || !best_ask())
        return std::nullopt;
    const Wide sum = static_cast<Wide>(bids_.begin()->first.value()) + asks_.begin()->first.value();
    return Midpoint{PriceTicks{static_cast<std::int64_t>(sum / 2)}, sum % 2 != 0};
}
std::optional<PriceTicks> OrderBook::mid_price() const {
    const auto mid = midpoint();
    return mid ? std::optional<PriceTicks>{mid->floor_ticks} : std::nullopt;
}
std::optional<PriceTicks> OrderBook::microprice() const {
    const auto *bid = best_level(Side::Buy);
    const auto *ask = best_level(Side::Sell);
    if (!bid || !ask)
        return std::nullopt;
    const Wide bq = bid->total_quantity.value(), aq = ask->total_quantity.value();
    const Wide numerator = aq * bids_.begin()->first.value() + bq * asks_.begin()->first.value();
    return PriceTicks{static_cast<std::int64_t>(numerator / (bq + aq))};
}
DepthView OrderBook::depth(Side side, std::size_t levels) const {
    check_side(side);
    DepthView result;
    auto collect = [&](const auto &prices) {
        for (const auto &[price, level] : prices) {
            if (result.size() == levels)
                break;
            result.push_back({price, level.total_quantity});
        }
    };
    if (side == Side::Buy)
        collect(bids_);
    else
        collect(asks_);
    return result;
}
std::vector<Order> OrderBook::orders_at(Side side, PriceTicks price) const {
    check_side(side);
    auto collect = [&](const auto &prices) -> std::vector<Order> {
        const auto it = prices.find(price);
        if (it == prices.end())
            return {};
        return {it->second.orders.begin(), it->second.orders.end()};
    };
    return side == Side::Buy ? collect(bids_) : collect(asks_);
}
double OrderBook::imbalance(std::size_t levels) const {
    // Reporting metric only; never authoritative prices/quantities.
    long double bid = 0, ask = 0;
    auto sum = [levels](const auto &prices) {
        long double total = 0;
        std::size_t count = 0;
        for (const auto &[price, level] : prices) {
            (void)price;
            if (count++ == levels)
                break;
            total += static_cast<long double>(level.total_quantity.value());
        }
        return total;
    };
    bid = sum(bids_);
    ask = sum(asks_);
    return bid + ask == 0 ? 0.0 : static_cast<double>((bid - ask) / (bid + ask));
}
std::optional<PriceTicks> OrderBook::vwap(Side side, QuantityLots requested) const {
    check_side(side);
    if (requested.value() <= 0)
        throw std::invalid_argument("VWAP quantity must be positive");
    Wide notional = 0;
    auto remaining = requested.value();
    auto walk = [&](const auto &prices) {
        for (const auto &[price, level] : prices) {
            const auto taken = std::min(remaining, level.total_quantity.value());
            notional += static_cast<Wide>(price.value()) * taken;
            remaining -= taken;
            if (remaining == 0)
                break;
        }
    };
    if (side == Side::Buy)
        walk(bids_);
    else
        walk(asks_);
    if (remaining != 0)
        return std::nullopt;
    return PriceTicks{static_cast<std::int64_t>(notional / requested.value())};
}
bool OrderBook::valid() const {
    if (best_bid() && best_ask() && *best_bid() >= *best_ask())
        return false;
    std::unordered_set<OrderId, IdHash> seen;
    auto check = [&](const auto &prices, Side side) {
        for (const auto &[price, level] : prices) {
            if (level.orders.empty() || level.total_quantity.value() <= 0)
                return false;
            Wide total = 0;
            for (const auto &order : level.orders) {
                if (order.id.value() == 0 || order.price != price || order.side != side ||
                    order.quantity.value() <= 0 || !seen.insert(order.id).second)
                    return false;
                const auto found = index_.find(order.id);
                if (found == index_.end() || found->second.level != &level ||
                    &*found->second.order != &order)
                    return false;
                total += order.quantity.value();
            }
            if (total != level.total_quantity.value())
                return false;
        }
        return true;
    };
    return check(bids_, Side::Buy) && check(asks_, Side::Sell) && seen.size() == index_.size();
}
} // namespace microback
