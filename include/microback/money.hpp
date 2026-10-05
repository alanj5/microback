#ifndef MICROBACK_MONEY_HPP
#define MICROBACK_MONEY_HPP
#include "microback/types.hpp"
#include <stdexcept>
namespace microback {
using MoneyAtoms = __int128;
inline constexpr MoneyAtoms money_scale = 1000000;
inline MoneyAtoms money_add(MoneyAtoms a, MoneyAtoms b) {
    MoneyAtoms result;
    if (__builtin_add_overflow(a, b, &result))
        throw std::overflow_error("money addition overflow");
    return result;
}
inline MoneyAtoms money_mul(MoneyAtoms a, MoneyAtoms b) {
    MoneyAtoms result;
    if (__builtin_mul_overflow(a, b, &result))
        throw std::overflow_error("money multiplication overflow");
    return result;
}
inline MoneyAtoms notional_atoms(PriceTicks price, QuantityLots qty) {
    return money_mul(money_mul(price.value(), qty.value()), money_scale);
}
} // namespace microback
#endif
