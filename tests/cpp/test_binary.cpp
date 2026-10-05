#include "microback/binary.hpp"
#include "microback/strategy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sstream>
using namespace microback;
namespace {
std::string fixture() {
    std::ostringstream out;
    write_binary_header(out);
    CsvEventSource csv{std::string{MICROBACK_FIXTURE_DIR} + "/replay.csv"};
    MarketEvent e;
    while (csv.next(e))
        write_binary_event(out, e);
    return out.str();
}
} // namespace
TEST_CASE("Binary records have exact portable bytes and replay equivalence") {
    std::ostringstream out;
    write_binary_header(out);
    const MarketEvent e{
        Timestamp{0x0102030405060708}, Sequence{2}, OrderId{3}, PriceTicks{100}, QuantityLots{5},
        MarketEventType::Add,          Side::Sell};
    write_binary_event(out, e);
    const auto bytes = out.str();
    REQUIRE(bytes.size() == 64);
    REQUIRE(bytes.substr(0, 8) == "MBACK001");
    REQUIRE(static_cast<unsigned char>(bytes[16]) == 8);
    REQUIRE(static_cast<unsigned char>(bytes[23]) == 1);
    REQUIRE(bytes[56] == 1);
    REQUIRE(bytes[57] == 1);
    REQUIRE(bytes[60] == 1);
    std::istringstream in{bytes};
    BinaryEventSource source{in};
    MarketEvent decoded;
    REQUIRE(source.next(decoded));
    REQUIRE(decoded == e);
    REQUIRE_FALSE(source.next(decoded));
    std::istringstream stream{fixture()};
    BinaryEventSource bin{stream};
    CsvEventSource csv{std::string{MICROBACK_FIXTURE_DIR} + "/replay.csv"};
    MarketMaker a, b;
    REQUIRE(run_backtest(bin, a) == run_backtest(csv, b));
}
TEST_CASE("Binary rejects unknown framing fields records and truncation") {
    const auto good = fixture();
    for (const auto offset : {0U, 8U, 10U, 12U, 56U, 57U, 58U, 60U}) {
        auto bad = good;
        bad[offset] = static_cast<char>(255);
        std::istringstream stream{bad};
        REQUIRE_THROWS([&] {
            BinaryEventSource source{stream};
            MarketEvent e;
            while (source.next(e)) {
            }
        }());
    }
    for (const auto size : {0U, 15U, 17U, 63U}) {
        std::istringstream stream{good.substr(0, size)};
        REQUIRE_THROWS([&] {
            BinaryEventSource source{stream};
            MarketEvent e;
            while (source.next(e)) {
            }
        }());
    }
    auto bad = good;
    bad[23] = static_cast<char>(128);
    std::istringstream stream{bad};
    BinaryEventSource source{stream};
    MarketEvent e;
    REQUIRE_THROWS(source.next(e));
}
