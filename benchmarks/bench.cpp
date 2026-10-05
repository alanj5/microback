#include "microback/binary.hpp"
#include "microback/execution.hpp"
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <filesystem>
#include <limits>
using namespace microback;
namespace {
struct Workload {
    std::size_t count, levels;
};
std::vector<MarketEvent> stream(Workload shape) {
    const auto [count, levels] = shape;
    std::vector<MarketEvent> out;
    out.reserve(count);
    auto emit = [&](std::int64_t t, std::uint64_t id, std::int64_t p, std::int64_t q,
                    MarketEventType type, Side side) {
        out.push_back({Timestamp{t}, Sequence{out.size() + 1}, OrderId{id}, PriceTicks{p},
                       QuantityLots{q}, type, side});
    };
    emit(0, 0, 0, 0, MarketEventType::Snapshot, Side::Buy);
    for (const auto side : {Side::Buy, Side::Sell})
        for (std::size_t l = 0; l < levels; ++l) {
            const auto n = static_cast<std::int64_t>(l);
            emit(0, (side == Side::Buy ? 0 : levels) + l + 1,
                 side == Side::Buy ? 10000 - n : 10002 + n, 100 + n, MarketEventType::Snapshot,
                 side);
        }
    emit(0, 0, 0, 0, MarketEventType::Snapshot, Side::Sell);
    while (out.size() < count) {
        const auto i = out.size() - 2 * levels - 2;
        const auto side = i % 2 == 0 ? Side::Buy : Side::Sell;
        const auto l = (i / 5) % levels;
        const auto n = static_cast<std::int64_t>(l);
        const auto p = side == Side::Buy ? 10000 - n : 10002 + n;
        const auto q = ((i / 100 + 7) % 2 == static_cast<std::size_t>(side) ? 400 : 40) +
                       static_cast<std::int64_t>((i * 17 + 7) % 31);
        emit((static_cast<std::int64_t>(i) + 1) * 100000,
             i % 5 == 0 ? 0 : (side == Side::Buy ? 0 : levels) + l + 1, p, i % 5 == 0 ? 150 : q,
             i % 5 == 0 ? MarketEventType::Trade : MarketEventType::Modify, side);
    }
    return out;
}
void rates(benchmark::State &s, std::int64_t n) {
    s.SetItemsProcessed(s.iterations() * n);
    s.counters["events_per_second"] =
        benchmark::Counter(static_cast<double>(s.iterations() * n), benchmark::Counter::kIsRate);
    s.counters["seconds_per_item"] =
        benchmark::Counter(static_cast<double>(s.iterations() * n),
                           benchmark::Counter::kIsRate | benchmark::Counter::kInvert);
}
void add(benchmark::State &s) {
    OrderBook book;
    for (auto _ : s) {
        (void)_;
        for (std::uint64_t i = 1; i <= 1024; ++i)
            book.add({OrderId{i}, Side::Buy, PriceTicks{100}, QuantityLots{1}});
        s.PauseTiming();
        book.clear();
        s.ResumeTiming();
    }
    rates(s, 1024);
}
void cancel(benchmark::State &s) {
    OrderBook book;
    for (auto _ : s) {
        (void)_;
        s.PauseTiming();
        for (std::uint64_t i = 1; i <= 1024; ++i)
            book.add({OrderId{i}, Side::Buy, PriceTicks{100}, QuantityLots{1}});
        s.ResumeTiming();
        for (std::uint64_t i = 1; i <= 1024; ++i)
            book.cancel(OrderId{i});
    }
    rates(s, 1024);
}
void modify(benchmark::State &s) {
    OrderBook book;
    book.add({OrderId{1}, Side::Buy, PriceTicks{100}, QuantityLots{1}});
    std::int64_t q = 1;
    for (auto _ : s) {
        (void)_;
        q = 3 - q;
        book.modify({OrderId{1}, PriceTicks{100}, QuantityLots{q}});
    }
    rates(s, 1);
}
void best(benchmark::State &s) {
    OrderBook book;
    book.add({OrderId{1}, Side::Buy, PriceTicks{100}, QuantityLots{1}});
    book.add({OrderId{2}, Side::Sell, PriceTicks{102}, QuantityLots{1}});
    for (auto _ : s) {
        (void)_;
        benchmark::DoNotOptimize(book.best_bid());
        benchmark::DoNotOptimize(book.best_ask());
    }
    rates(s, 1);
}
void trade(benchmark::State &s) {
    OrderBook book;
    book.add({OrderId{1}, Side::Buy, PriceTicks{100},
              QuantityLots{std::numeric_limits<std::int64_t>::max()}});
    for (auto _ : s) {
        (void)_;
        book.apply({Timestamp{1}, Sequence{1}, OrderId{1}, PriceTicks{100}, QuantityLots{1},
                    MarketEventType::Trade, Side::Buy});
    }
    rates(s, 1);
}
void walk(benchmark::State &s) {
    OrderBook book;
    for (std::uint64_t i = 1; i <= 100; ++i)
        book.add({OrderId{i}, Side::Sell, PriceTicks{static_cast<std::int64_t>(100 + i)},
                  QuantityLots{1}});
    for (auto _ : s) {
        (void)_;
        benchmark::DoNotOptimize(book.vwap(Side::Sell, QuantityLots{100}));
    }
    rates(s, 1);
}
void crossing_walk(benchmark::State &s) {
    const auto events = stream({202, 100});
    for (auto _ : s) {
        (void)_;
        s.PauseTiming();
        auto engine = std::make_unique<ExecutionEngine>();
        for (const auto &e : events)
            engine->on_market_event(e);
        engine->submit(Side::Buy, OrderType::Market, TimeInForce::IOC, QuantityLots{10000});
        s.ResumeTiming();
        engine->flush();
        auto count = engine->fills().size();
        benchmark::DoNotOptimize(count);
        s.PauseTiming();
        engine.reset();
        s.ResumeTiming();
    }
    rates(s, 1);
}
void replay(benchmark::State &s) {
    const auto count = s.range(0);
    const auto events = stream({static_cast<std::size_t>(count), 10});
    for (auto _ : s) {
        (void)_;
        ReplayEngine engine;
        for (const auto &e : events)
            engine.ingest(e);
        engine.finish();
        auto stats = engine.statistics();
        benchmark::DoNotOptimize(stats);
    }
    rates(s, count);
}
void execution(benchmark::State &s) {
    const auto count = s.range(0);
    const auto events =
        stream({static_cast<std::size_t>(count), static_cast<std::size_t>(s.range(1))});
    for (auto _ : s) {
        (void)_;
        ExecutionEngine engine;
        for (const auto &e : events)
            engine.on_market_event(e);
        engine.finish();
        auto portfolio = engine.portfolio().state();
        benchmark::DoNotOptimize(portfolio);
    }
    rates(s, count);
}
void file_replay(benchmark::State &s, bool binary) {
    const char *env = std::getenv("MICROBACK_DATA_DIR");
    const auto directory = env ? std::string{env} : "data/generated";
    const auto path = directory + "/" + std::to_string(s.range(0)) + (binary ? ".mkt" : ".csv");
    if (!std::filesystem::exists(path)) {
        s.SkipWithError(("generate required file: " + path).c_str());
        return;
    }
    for (auto _ : s) {
        (void)_;
        auto source = open_event_source(path);
        ReplayEngine engine;
        engine.run(*source);
        if (engine.statistics().events != static_cast<std::uint64_t>(s.range(0))) {
            s.SkipWithError("incorrect event count");
            break;
        }
        auto stats = engine.statistics();
        benchmark::DoNotOptimize(stats);
    }
    rates(s, s.range(0));
}
void csv(benchmark::State &s) { file_replay(s, false); }
void binary(benchmark::State &s) { file_replay(s, true); }
} // namespace
// Google Benchmark registration deliberately uses library-owned static initializers.
// NOLINTBEGIN(bugprone-throwing-static-initialization)
BENCHMARK(add);
BENCHMARK(cancel);
BENCHMARK(modify);
BENCHMARK(best);
BENCHMARK(trade);
BENCHMARK(walk);
BENCHMARK(crossing_walk);
BENCHMARK(replay)->Arg(1000000)->Arg(10000000)->Unit(benchmark::kMillisecond);
BENCHMARK(execution)->Args({1000000, 10})->Args({1000000, 100})->Unit(benchmark::kMillisecond);
BENCHMARK(csv)->Arg(1000000)->Arg(10000000)->Unit(benchmark::kMillisecond);
BENCHMARK(binary)->Arg(1000000)->Arg(10000000)->Unit(benchmark::kMillisecond);
// NOLINTEND(bugprone-throwing-static-initialization)
BENCHMARK_MAIN();
