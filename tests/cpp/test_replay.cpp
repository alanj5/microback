#include "microback/replay_engine.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <stdexcept>
using namespace microback;
namespace {
MarketEvent event(std::int64_t time, std::uint64_t seq, std::uint64_t id, std::int64_t price,
                  std::int64_t qty, MarketEventType type, Side side = Side::Buy) {
    return {Timestamp{time},   Sequence{seq}, OrderId{id}, PriceTicks{price},
            QuantityLots{qty}, type,          side};
}
std::vector<MarketEvent> fixture() {
    return {event(0, 1, 0, 0, 0, MarketEventType::Snapshot),
            event(0, 2, 1, 100, 10, MarketEventType::Snapshot),
            event(0, 3, 2, 102, 12, MarketEventType::Snapshot, Side::Sell),
            event(0, 4, 0, 0, 0, MarketEventType::Snapshot, Side::Sell)};
}
void initialize(ReplayEngine &engine) {
    InMemoryEventSource source{fixture()};
    engine.run(source);
}
} // namespace
TEST_CASE("Snapshot state is published atomically") {
    ReplayEngine e;
    const auto events = fixture();
    for (std::size_t i = 0; i < 3; ++i) {
        REQUIRE_FALSE(e.ingest(events[i]));
        REQUIRE(e.book().order_count() == 0);
    }
    REQUIRE(e.ingest(events[3]));
    e.finish();
    REQUIRE(e.book().valid());
    REQUIRE(e.book().best_bid() == PriceTicks{100});
    REQUIRE(e.book().best_ask() == PriceTicks{102});
    REQUIRE(e.statistics() == ReplayStatistics{4, 1, 0});
    REQUIRE_FALSE(e.ingest(event(10, 5, 0, 0, 0, MarketEventType::Snapshot)));
    REQUIRE(e.book().order_count() == 2);
    REQUIRE_FALSE(e.ingest(event(10, 6, 3, 90, 5, MarketEventType::Snapshot)));
    REQUIRE(e.ingest(event(10, 7, 0, 0, 0, MarketEventType::Snapshot, Side::Sell)));
    REQUIRE(e.book().order_count() == 1);
    REQUIRE_FALSE(e.book().best_ask());
    REQUIRE(e.statistics().snapshots == 2);
}
TEST_CASE("Canonical fixture replays identically from CSV and memory") {
    ReplayEngine a, b;
    CsvEventSource source{std::string{MICROBACK_FIXTURE_DIR} + "/replay.csv"};
    a.run(source);
    CsvEventSource read{std::string{MICROBACK_FIXTURE_DIR} + "/replay.csv"};
    std::vector<MarketEvent> events;
    MarketEvent value{};
    while (read.next(value))
        events.push_back(value);
    InMemoryEventSource memory{events};
    b.run(memory);
    REQUIRE(a.book().valid());
    REQUIRE(a.statistics() == b.statistics());
    REQUIRE(a.statistics() == ReplayStatistics{9, 1, 0});
    REQUIRE(a.book().depth(Side::Buy, 10) == b.book().depth(Side::Buy, 10));
    REQUIRE(a.book().depth(Side::Sell, 10) == b.book().depth(Side::Sell, 10));
    REQUIRE(a.book().find(OrderId{1}).value_or(Order{}).quantity == QuantityLots{8});
    REQUIRE(a.book().find(OrderId{2}).value_or(Order{}).quantity == QuantityLots{10});
    REQUIRE_FALSE(a.book().find(OrderId{3}));
    REQUIRE(a.book().order_count() == 2);
}
TEST_CASE("Replay rejects regressions and requires explicit recovery") {
    for (auto invalid :
         {event(0, 4, 3, 99, 1, MarketEventType::Add), event(0, 3, 3, 99, 1, MarketEventType::Add),
          event(-1, 5, 3, 99, 1, MarketEventType::Add)}) {
        ReplayEngine e;
        initialize(e);
        REQUIRE_THROWS(e.ingest(invalid));
        REQUIRE(e.failed());
        REQUIRE_THROWS_AS(e.ingest(event(10, 5, 3, 99, 1, MarketEventType::Add)), std::logic_error);
        e.reset();
        initialize(e);
        REQUIRE(e.book().valid());
    }
    ReplayEngine e;
    initialize(e);
    e.ingest(event(10, 7, 3, 99, 1, MarketEventType::Add));
    REQUIRE(e.statistics().sequence_gaps == 2);
    REQUIRE_THROWS(e.ingest(event(9, 8, 4, 98, 1, MarketEventType::Add)));
}
TEST_CASE("Snapshot protocol and malformed market updates fail fast") {
    const auto begin = event(0, 1, 0, 0, 0, MarketEventType::Snapshot);
    for (auto invalid : {event(0, 1, 1, 100, 1, MarketEventType::Add),
                         event(0, 1, 1, 100, 1, MarketEventType::Snapshot),
                         event(0, 1, 0, 0, 0, MarketEventType::Snapshot, Side::Sell)}) {
        ReplayEngine e;
        REQUIRE_THROWS(e.ingest(invalid));
    }
    ReplayEngine e;
    e.ingest(begin);
    REQUIRE_THROWS(e.finish());
    REQUIRE(e.failed());
    ReplayEngine timed;
    timed.ingest(begin);
    REQUIRE_THROWS(timed.ingest(event(1, 2, 1, 100, 1, MarketEventType::Snapshot)));
    ReplayEngine nested;
    nested.ingest(begin);
    REQUIRE_THROWS(nested.ingest(event(0, 2, 0, 0, 0, MarketEventType::Snapshot)));
    ReplayEngine incremental;
    incremental.ingest(begin);
    REQUIRE_THROWS(incremental.ingest(event(0, 2, 1, 100, 1, MarketEventType::Add)));
    ReplayEngine crossed;
    crossed.ingest(begin);
    crossed.ingest(event(0, 2, 1, 103, 1, MarketEventType::Snapshot));
    crossed.ingest(event(0, 3, 2, 102, 1, MarketEventType::Snapshot, Side::Sell));
    REQUIRE_THROWS(crossed.ingest(event(0, 4, 0, 0, 0, MarketEventType::Snapshot, Side::Sell)));
    REQUIRE(crossed.book().order_count() == 0);
    for (auto invalid : {event(1, 5, 3, 102, 1, MarketEventType::Add),
                         event(1, 5, 99, 0, 0, MarketEventType::Cancel),
                         event(1, 5, 1, 100, 1, MarketEventType::Modify, Side::Sell),
                         event(1, 5, 2, 102, 13, MarketEventType::Trade, Side::Sell),
                         event(1, 5, 3, 100, -1, MarketEventType::Add),
                         event(1, 5, 3, 100, 1, static_cast<MarketEventType>(99))}) {
        ReplayEngine fresh;
        initialize(fresh);
        REQUIRE_THROWS(fresh.ingest(invalid));
        REQUIRE(fresh.book().valid());
    }
}
TEST_CASE("Full trade removes order and L2 print is non-mutating") {
    ReplayEngine e;
    initialize(e);
    e.ingest(event(1, 5, 0, 102, 12, MarketEventType::Trade, Side::Sell));
    REQUIRE(e.book().find(OrderId{2}).value_or(Order{}).quantity == QuantityLots{12});
    e.ingest(event(2, 6, 2, 102, 12, MarketEventType::Trade, Side::Sell));
    REQUIRE_FALSE(e.book().best_ask());
    REQUIRE(e.book().valid());
}
TEST_CASE("CSV strict parser reports schema integer and row failures") {
    for (const std::string row : {"0,1,1,100,1,Unknown,Buy", "0,1,1,100,1,Add,Unknown",
                                  "0,1,1,100,-1,Add,Buy", "0,1,1,100,1,Add,Buy,extra",
                                  "0,1,1,100,Add,Buy", "0,18446744073709551616,1,100,1,Add,Buy",
                                  "0,1,1,100.0,1,Add,Buy", "0,1,1,100,1,Add,Buy ", ""}) {
        std::istringstream stream{
            "timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side\n" + row + "\n"};
        CsvEventSource source{stream};
        MarketEvent e{};
        REQUIRE_THROWS(source.next(e));
    }
    std::istringstream bad{"wrong header\n"};
    REQUIRE_THROWS_AS(CsvEventSource{bad}, std::invalid_argument);
    std::istringstream empty{};
    REQUIRE_THROWS_AS(CsvEventSource{empty}, std::invalid_argument);
    REQUIRE_THROWS_AS(CsvEventSource{std::string{"/nonexistent/microback.csv"}},
                      std::runtime_error);
}
