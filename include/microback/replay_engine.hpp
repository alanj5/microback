#ifndef MICROBACK_REPLAY_ENGINE_HPP
#define MICROBACK_REPLAY_ENGINE_HPP
#include "microback/event.hpp"
#include "microback/order_book.hpp"
#include <memory>
#include <optional>
namespace microback {
struct ReplayStatistics {
    std::uint64_t events = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t sequence_gaps = 0;
    bool operator==(const ReplayStatistics &) const = default;
};
class ReplayEngine {
    std::unique_ptr<OrderBook> book_ = std::make_unique<OrderBook>();
    std::unique_ptr<OrderBook> snapshot_;
    std::optional<MarketEvent> previous_;
    Timestamp snapshot_time_;
    ReplayStatistics stats_;
    bool initialized_ = false;
    bool failed_ = false;

  public:
    // True only when an event publishes a complete observable market state.
    bool ingest(const MarketEvent &);
    void finish();
    void run(EventSource &);
    void reset();
    [[nodiscard]] const OrderBook &book() const noexcept { return *book_; }
    [[nodiscard]] const ReplayStatistics &statistics() const noexcept { return stats_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
};
} // namespace microback
#endif
