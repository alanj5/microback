#ifndef MICROBACK_BINARY_HPP
#define MICROBACK_BINARY_HPP
#include "microback/event.hpp"
#include <memory>
#include <ostream>
namespace microback {
inline constexpr std::uint32_t binary_symbol_id = 1;
class BinaryEventSource final : public EventSource {
    std::ifstream file_;
    std::istream *stream_;
    void read_header();

  public:
    explicit BinaryEventSource(const std::string &);
    explicit BinaryEventSource(std::istream &);
    BinaryEventSource(const BinaryEventSource &) = delete;
    BinaryEventSource &operator=(const BinaryEventSource &) = delete;
    bool next(MarketEvent &) override;
};
void write_binary_header(std::ostream &);
void write_binary_event(std::ostream &, const MarketEvent &);
std::unique_ptr<EventSource> open_event_source(const std::string &);
} // namespace microback
#endif
