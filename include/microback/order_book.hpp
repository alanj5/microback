#ifndef MICROBACK_ORDER_BOOK_HPP
#define MICROBACK_ORDER_BOOK_HPP

#include "microback/types.hpp"
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace microback {
struct MarketEvent;
struct Order {
    OrderId id;
    Side side;
    PriceTicks price;
    QuantityLots quantity;
    bool operator==(const Order &) const = default;
};
using AddOrder = Order;
struct ModifyOrder {
    OrderId id;
    PriceTicks price;
    QuantityLots quantity;
};
struct PriceLevel {
    QuantityLots total_quantity;
    std::list<Order> orders;
};
struct DepthLevel {
    PriceTicks price;
    QuantityLots quantity;
    bool operator==(const DepthLevel &) const = default;
};
using DepthView = std::vector<DepthLevel>;
struct Midpoint {
    PriceTicks floor_ticks;
    bool half_tick;
    bool operator==(const Midpoint &) const = default;
    [[nodiscard]] double as_double() const noexcept {
        return static_cast<double>(floor_ticks.value()) + (half_tick ? 0.5 : 0.0);
    }
};

class OrderBook {
    using Bids = std::map<PriceTicks, PriceLevel, std::greater<>>;
    using Asks = std::map<PriceTicks, PriceLevel, std::less<>>;
    struct IdHash {
        std::size_t operator()(OrderId id) const noexcept {
            return std::hash<std::uint64_t>{}(id.value());
        }
    };
    struct OrderLocation {
        PriceLevel *level;
        std::list<Order>::iterator order;
    };
    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, OrderLocation, IdHash> index_;

    template <class Levels> void insert(Levels &, const AddOrder &);
    [[nodiscard]] const PriceLevel *best_level(Side) const;

  public:
    OrderBook() = default;
    // Index locations refer to this instance's stable lists and map nodes.
    OrderBook(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &) = delete;
    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;
    void add(const AddOrder &);
    void apply(const MarketEvent &);
    void cancel(OrderId);
    void modify(const ModifyOrder &);
    void clear() noexcept;
    [[nodiscard]] std::optional<Order> find(OrderId) const;
    [[nodiscard]] std::size_t order_count() const noexcept { return index_.size(); }
    [[nodiscard]] std::optional<PriceTicks> best_bid() const;
    [[nodiscard]] std::optional<PriceTicks> best_ask() const;
    [[nodiscard]] std::optional<PriceTicks> spread_ticks() const;
    [[nodiscard]] std::optional<Midpoint> midpoint() const;
    [[nodiscard]] std::optional<PriceTicks> mid_price() const;
    [[nodiscard]] std::optional<PriceTicks> microprice() const;
    [[nodiscard]] double imbalance(std::size_t levels) const;
    [[nodiscard]] DepthView depth(Side, std::size_t levels) const;
    [[nodiscard]] std::vector<Order> orders_at(Side, PriceTicks) const;
    [[nodiscard]] std::optional<PriceTicks> vwap(Side liquidity_side, QuantityLots) const;
    [[nodiscard]] bool valid() const;
};
} // namespace microback
#endif
