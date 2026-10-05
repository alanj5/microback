#include "microback/types.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

using namespace microback;

template <class A, class B>
concept Addable = requires(A a, B b) { a + b; };
template <class A, class B>
concept Comparable = requires(A a, B b) { a == b; };

static_assert(!std::is_convertible_v<std::int64_t, PriceTicks>);
static_assert(!std::is_convertible_v<PriceTicks, std::int64_t>);
static_assert(!std::is_constructible_v<PriceTicks, QuantityLots>);
static_assert(!Addable<PriceTicks, QuantityLots>);
static_assert(!Comparable<Timestamp, PriceTicks>);
static_assert(std::is_same_v<OrderId::representation, std::uint64_t>);
static_assert(std::is_same_v<Sequence::representation, std::uint64_t>);
static_assert(std::is_same_v<Timestamp::representation, std::int64_t>);

TEST_CASE("Integer domains preserve values and comparisons") {
    REQUIRE(PriceTicks{}.value() == 0);
    REQUIRE(PriceTicks{12} == PriceTicks{12});
    REQUIRE(PriceTicks{11} < PriceTicks{12});
    REQUIRE(QuantityLots{3}.value() == 3);
    REQUIRE(Timestamp{-1}.value() == -1);
    REQUIRE(OrderId{std::numeric_limits<std::uint64_t>::max()}.value() ==
            std::numeric_limits<std::uint64_t>::max());
    REQUIRE(Sequence{9} > Sequence{8});
    REQUIRE(PriceTicks{std::numeric_limits<std::int64_t>::min()}.value() ==
            std::numeric_limits<std::int64_t>::min());
    REQUIRE(PriceTicks{std::numeric_limits<std::int64_t>::max()}.value() ==
            std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("Enums are compact and preserve canonical event values") {
    STATIC_REQUIRE(sizeof(Side) == 1);
    STATIC_REQUIRE(sizeof(MarketEventType) == 1);
    STATIC_REQUIRE(sizeof(OrderType) == 1);
    STATIC_REQUIRE(sizeof(TimeInForce) == 1);
    STATIC_REQUIRE(sizeof(OrderStatus) == 1);
    REQUIRE(static_cast<int>(MarketEventType::Snapshot) == 0);
    REQUIRE(static_cast<int>(MarketEventType::Trade) == 4);
    REQUIRE(static_cast<int>(OrderStatus::Rejected) == 5);
}
