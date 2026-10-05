#include "microback/event.hpp"
#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>
namespace microback {
namespace {
constexpr std::string_view header =
    "timestamp_ns,sequence,order_id,price_ticks,quantity_lots,type,side";
template <class T> T integer(std::string_view text) {
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid integer field");
    return value;
}
MarketEvent parse(const std::string &line) {
    std::array<std::string_view, 7> fields;
    const std::string_view view{line};
    std::size_t start = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto end = view.find(',', start);
        if ((i == fields.size() - 1) != (end == std::string_view::npos))
            throw std::invalid_argument("expected exactly seven CSV fields");
        fields[i] = view.substr(start, end == std::string_view::npos ? end : end - start);
        start = end == std::string_view::npos ? view.size() : end + 1;
    }
    MarketEventType type;
    if (fields[5] == "Snapshot")
        type = MarketEventType::Snapshot;
    else if (fields[5] == "Add")
        type = MarketEventType::Add;
    else if (fields[5] == "Modify")
        type = MarketEventType::Modify;
    else if (fields[5] == "Cancel")
        type = MarketEventType::Cancel;
    else if (fields[5] == "Trade")
        type = MarketEventType::Trade;
    else
        throw std::invalid_argument("unknown event type");
    Side side;
    if (fields[6] == "Buy")
        side = Side::Buy;
    else if (fields[6] == "Sell")
        side = Side::Sell;
    else
        throw std::invalid_argument("unknown side");
    MarketEvent event{Timestamp{integer<std::int64_t>(fields[0])},
                      Sequence{integer<std::uint64_t>(fields[1])},
                      OrderId{integer<std::uint64_t>(fields[2])},
                      PriceTicks{integer<std::int64_t>(fields[3])},
                      QuantityLots{integer<std::int64_t>(fields[4])},
                      type,
                      side};
    validate_event(event);
    return event;
}
void strip_cr(std::string &line) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}
} // namespace
void validate_event(const MarketEvent &e) {
    if (e.timestamp_ns.value() < 0 || e.sequence.value() == 0 || e.price.value() < 0 ||
        e.quantity.value() < 0)
        throw std::invalid_argument("negative market field or zero sequence");
    if (e.side != Side::Buy && e.side != Side::Sell)
        throw std::invalid_argument("invalid side");
    switch (e.type) {
    case MarketEventType::Snapshot:
        if (e.order_id.value() == 0) {
            if (e.price.value() != 0 || e.quantity.value() != 0)
                throw std::invalid_argument("snapshot marker has payload");
            return;
        }
        break;
    case MarketEventType::Add:
    case MarketEventType::Modify:
        if (e.order_id.value() == 0)
            throw std::invalid_argument("zero ID on book update");
        break;
    case MarketEventType::Cancel:
        if (e.order_id.value() == 0 || e.price.value() != 0 || e.quantity.value() != 0)
            throw std::invalid_argument("cancel needs nonzero ID and zero price/quantity");
        return;
    case MarketEventType::Trade:
        break;
    default:
        throw std::invalid_argument("invalid event type");
    }
    if (e.price.value() == 0 || e.quantity.value() == 0)
        throw std::invalid_argument("nonpositive market update");
}
bool InMemoryEventSource::next(MarketEvent &event) {
    if (cursor_ == events_.size())
        return false;
    event = events_[cursor_++];
    return true;
}
CsvEventSource::CsvEventSource(const std::string &path) : file_(path), stream_(&file_) {
    if (!file_)
        throw std::runtime_error("cannot open CSV: " + path);
    read_header();
}
CsvEventSource::CsvEventSource(std::istream &stream) : stream_(&stream) { read_header(); }
void CsvEventSource::read_header() {
    std::string line;
    if (!std::getline(*stream_, line))
        throw std::invalid_argument("missing CSV header");
    ++line_;
    strip_cr(line);
    if (line != header)
        throw std::invalid_argument("invalid CSV header");
}
bool CsvEventSource::next(MarketEvent &event) {
    std::string line;
    if (!std::getline(*stream_, line)) {
        if (stream_->bad() || !stream_->eof())
            throw std::runtime_error("CSV read failed");
        return false;
    }
    ++line_;
    strip_cr(line);
    try {
        event = parse(line);
    } catch (const std::invalid_argument &error) {
        throw std::invalid_argument("CSV line " + std::to_string(line_) + ": " + error.what());
    }
    return true;
}
} // namespace microback
