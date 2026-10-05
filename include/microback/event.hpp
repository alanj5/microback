#ifndef MICROBACK_EVENT_HPP
#define MICROBACK_EVENT_HPP
#include "microback/types.hpp"
#include <fstream>
#include <istream>
#include <string>
#include <utility>
#include <vector>
namespace microback {
struct MarketEvent {
    Timestamp timestamp_ns;
    Sequence sequence;
    OrderId order_id;
    PriceTicks price;
    QuantityLots quantity;
    MarketEventType type;
    Side side;
    bool operator==(const MarketEvent &) const = default;
};
class EventSource {
  public:
    virtual ~EventSource() = default;
    virtual bool next(MarketEvent &) = 0;
};
class InMemoryEventSource final : public EventSource {
    std::vector<MarketEvent> events_;
    std::size_t cursor_ = 0;

  public:
    explicit InMemoryEventSource(std::vector<MarketEvent> events) : events_(std::move(events)) {}
    bool next(MarketEvent &) override;
};
class CsvEventSource final : public EventSource {
    std::ifstream file_;
    std::istream *stream_;
    std::size_t line_ = 0;
    void read_header();

  public:
    explicit CsvEventSource(const std::string &path);
    explicit CsvEventSource(std::istream &stream);
    CsvEventSource(const CsvEventSource &) = delete;
    CsvEventSource &operator=(const CsvEventSource &) = delete;
    CsvEventSource(CsvEventSource &&) = delete;
    CsvEventSource &operator=(CsvEventSource &&) = delete;
    bool next(MarketEvent &) override;
};
void validate_event(const MarketEvent &);
} // namespace microback
#endif
