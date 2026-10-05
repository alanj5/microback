#include "microback/risk.hpp"
namespace microback {
void validate_limits(const RiskLimits &limits) {
    if (limits.max_position.value() < 0 || limits.max_order_size.value() <= 0 ||
        limits.max_notional_atoms < 0)
        throw std::invalid_argument("invalid risk limits");
}
} // namespace microback
