#ifndef MICROBACK_RISK_HPP
#define MICROBACK_RISK_HPP
#include "microback/money.hpp"
#include <limits>
namespace microback {
struct RiskLimits {
    QuantityLots max_position{std::numeric_limits<std::int64_t>::max()};
    QuantityLots max_order_size{std::numeric_limits<std::int64_t>::max()};
    // Zero disables the optional submission-notional cap. In tick*lot atoms.
    MoneyAtoms max_notional_atoms = 0;
};
enum class RejectionReason : std::uint8_t { OrderSize, Position, Notional, MissingPrice };
struct Rejection {
    OrderId id;
    Timestamp timestamp;
    RejectionReason reason;
    bool operator==(const Rejection &) const = default;
};
void validate_limits(const RiskLimits &);
} // namespace microback
#endif
