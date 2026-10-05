#include "microback/binary.hpp"
#include <array>
#include <bit>
#include <filesystem>
#include <stdexcept>
namespace microback {
namespace {
constexpr std::array<unsigned char, 16> header{'M', 'B', 'A', 'C', 'K', '0', '0', '1',
                                               1,   0,   48,  0,   1,   0,   0,   0};
template <std::size_t N> void read(std::istream &s, std::array<unsigned char, N> &bytes) {
    s.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(N));
    if (s.gcount() != static_cast<std::streamsize>(N) || !s)
        throw std::invalid_argument("truncated or unreadable binary stream");
}
template <std::size_t N> void write(std::ostream &s, const std::array<unsigned char, N> &bytes) {
    s.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(N));
    if (!s)
        throw std::runtime_error("binary write failed");
}
std::uint64_t load(const std::array<unsigned char, 48> &bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    return value;
}
void store(std::array<unsigned char, 48> &bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<unsigned char>((value >> (8 * i)) & 255U);
}
} // namespace
BinaryEventSource::BinaryEventSource(const std::string &path)
    : file_(path, std::ios::binary), stream_(&file_) {
    if (!file_)
        throw std::runtime_error("cannot open binary: " + path);
    read_header();
}
BinaryEventSource::BinaryEventSource(std::istream &s) : stream_(&s) { read_header(); }
void BinaryEventSource::read_header() {
    std::array<unsigned char, 16> bytes{};
    read(*stream_, bytes);
    if (bytes != header)
        throw std::invalid_argument("unsupported binary header/version/symbol");
}
bool BinaryEventSource::next(MarketEvent &e) {
    if (stream_->peek() == std::char_traits<char>::eof()) {
        if (stream_->bad() || !stream_->eof())
            throw std::runtime_error("binary read failed");
        return false;
    }
    std::array<unsigned char, 48> bytes{};
    read(*stream_, bytes);
    if (bytes[42] != 0 || bytes[43] != 0 || bytes[44] != 1 || bytes[45] != 0 || bytes[46] != 0 ||
        bytes[47] != 0)
        throw std::invalid_argument("unsupported binary flags/symbol");
    e = {Timestamp{std::bit_cast<std::int64_t>(load(bytes, 0))},
         Sequence{load(bytes, 8)},
         OrderId{load(bytes, 16)},
         PriceTicks{std::bit_cast<std::int64_t>(load(bytes, 24))},
         QuantityLots{std::bit_cast<std::int64_t>(load(bytes, 32))},
         static_cast<MarketEventType>(bytes[40]),
         static_cast<Side>(bytes[41])};
    validate_event(e);
    return true;
}
void write_binary_header(std::ostream &s) { write(s, header); }
void write_binary_event(std::ostream &s, const MarketEvent &e) {
    validate_event(e);
    std::array<unsigned char, 48> bytes{};
    store(bytes, 0, static_cast<std::uint64_t>(e.timestamp_ns.value()));
    store(bytes, 8, e.sequence.value());
    store(bytes, 16, e.order_id.value());
    store(bytes, 24, static_cast<std::uint64_t>(e.price.value()));
    store(bytes, 32, static_cast<std::uint64_t>(e.quantity.value()));
    bytes[40] = static_cast<unsigned char>(e.type);
    bytes[41] = static_cast<unsigned char>(e.side);
    bytes[44] = 1;
    write(s, bytes);
}
std::unique_ptr<EventSource> open_event_source(const std::string &path) {
    if (std::filesystem::path{path}.extension() == ".mkt")
        return std::make_unique<BinaryEventSource>(path);
    return std::make_unique<CsvEventSource>(path);
}
} // namespace microback
