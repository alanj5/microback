#ifndef MICROBACK_TYPES_HPP
#define MICROBACK_TYPES_HPP

#include <compare>
#include <cstdint>
#include <type_traits>

namespace microback {

// Intentionally no implicit conversion or arithmetic: different domains cannot mix.
// A negative raw integer is representable; operation boundaries validate domain rules.
template <class Tag, class Rep> class Integer {
    static_assert(std::is_integral_v<Rep>);
    Rep value_{};

  public:
    using representation = Rep;
    constexpr Integer() noexcept = default;
    explicit constexpr Integer(Rep value) noexcept : value_(value) {}
    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }
    constexpr auto operator<=>(const Integer &) const noexcept = default;
};

using PriceTicks = Integer<struct PriceTag, std::int64_t>;
using QuantityLots = Integer<struct QuantityTag, std::int64_t>;
using Timestamp = Integer<struct TimestampTag, std::int64_t>; // nanoseconds
using OrderId = Integer<struct OrderIdTag, std::uint64_t>;
using Sequence = Integer<struct SequenceTag, std::uint64_t>;

enum class Side : std::uint8_t { Buy, Sell };
enum class MarketEventType : std::uint8_t { Snapshot, Add, Modify, Cancel, Trade };
enum class OrderType : std::uint8_t { Market, Limit };
enum class TimeInForce : std::uint8_t { GTC, IOC };
enum class OrderStatus : std::uint8_t {
    Pending,
    Active,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

static_assert(sizeof(PriceTicks) == 8 && sizeof(QuantityLots) == 8 && sizeof(Timestamp) == 8);
static_assert(sizeof(OrderId) == 8 && sizeof(Sequence) == 8);
static_assert(std::is_trivially_copyable_v<PriceTicks>);
static_assert(std::is_trivially_copyable_v<QuantityLots>);
static_assert(std::is_trivially_copyable_v<Timestamp>);
static_assert(std::is_trivially_copyable_v<OrderId>);
static_assert(std::is_trivially_copyable_v<Sequence>);

} // namespace microback

#endif // MICROBACK_TYPES_HPP
