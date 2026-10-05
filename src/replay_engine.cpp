#include "microback/replay_engine.hpp"
#include <stdexcept>
namespace microback {
void OrderBook::apply(const MarketEvent &e) {
    validate_event(e);
    switch (e.type) {
    case MarketEventType::Snapshot:
        if (e.order_id.value() == 0) {
            if (e.side == Side::Buy)
                clear();
        } else
            add({e.order_id, e.side, e.price, e.quantity});
        break;
    case MarketEventType::Add:
        add({e.order_id, e.side, e.price, e.quantity});
        break;
    case MarketEventType::Modify: {
        const auto old = find(e.order_id);
        if (!old || old->side != e.side)
            throw std::invalid_argument("modify side/ID mismatch");
        modify({e.order_id, e.price, e.quantity});
        break;
    }
    case MarketEventType::Cancel: {
        const auto old = find(e.order_id);
        if (!old || old->side != e.side)
            throw std::invalid_argument("cancel side/ID mismatch");
        cancel(e.order_id);
        break;
    }
    case MarketEventType::Trade:
        if (e.order_id.value() != 0) {
            const auto old = find(e.order_id);
            if (!old || old->side != e.side || old->price != e.price || old->quantity < e.quantity)
                throw std::invalid_argument("trade order/price/side/quantity mismatch");
            if (old->quantity == e.quantity)
                cancel(e.order_id);
            else
                modify({e.order_id, e.price,
                        QuantityLots{old->quantity.value() - e.quantity.value()}});
        }
        break;
    }
}
bool ReplayEngine::ingest(const MarketEvent &event) {
    if (failed_)
        throw std::logic_error("replay failed; reset before reuse");
    try {
        validate_event(event);
        if (previous_ &&
            (event.timestamp_ns < previous_->timestamp_ns || event.sequence <= previous_->sequence))
            throw std::invalid_argument("timestamp/sequence regression");
        if (snapshot_ && event.timestamp_ns != snapshot_time_)
            throw std::invalid_argument("snapshot spans multiple timestamps");
        const bool marker = event.type == MarketEventType::Snapshot && event.order_id.value() == 0;
        bool published = false;
        if (marker && event.side == Side::Buy) {
            if (snapshot_)
                throw std::invalid_argument("nested snapshot");
            snapshot_ = std::make_unique<OrderBook>();
            snapshot_time_ = event.timestamp_ns;
        } else if (marker && event.side == Side::Sell) {
            if (!snapshot_)
                throw std::invalid_argument("snapshot end without begin");
            if (!snapshot_->valid())
                throw std::invalid_argument("invalid snapshot book");
            book_.swap(snapshot_);
            snapshot_.reset();
            snapshot_time_ = Timestamp{};
            initialized_ = true;
            ++stats_.snapshots;
            published = true;
        } else if (snapshot_) {
            if (event.type != MarketEventType::Snapshot)
                throw std::invalid_argument("incremental within snapshot");
            snapshot_->apply(event);
        } else {
            if (!initialized_)
                throw std::invalid_argument("incremental before complete snapshot");
            if (event.type == MarketEventType::Snapshot)
                throw std::invalid_argument("snapshot row without begin");
            if (event.type == MarketEventType::Add || event.type == MarketEventType::Modify) {
                const auto opposing =
                    event.side == Side::Buy ? book_->best_ask() : book_->best_bid();
                if (opposing &&
                    (event.side == Side::Buy ? event.price >= *opposing : event.price <= *opposing))
                    throw std::invalid_argument("crossed incremental book");
            }
            book_->apply(event);
            published = true;
        }
        if (previous_)
            stats_.sequence_gaps += event.sequence.value() - previous_->sequence.value() - 1;
        previous_ = event;
        ++stats_.events;
        return published;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ReplayEngine::finish() {
    if (failed_)
        throw std::logic_error("replay failed");
    if (snapshot_ || !initialized_) {
        failed_ = true;
        throw std::invalid_argument("incomplete or missing snapshot");
    }
}
void ReplayEngine::run(EventSource &source) {
    if (failed_)
        throw std::logic_error("replay failed; reset before reuse");
    try {
        MarketEvent event{};
        while (source.next(event))
            ingest(event);
        finish();
    } catch (...) {
        failed_ = true;
        throw;
    }
}
void ReplayEngine::reset() {
    book_ = std::make_unique<OrderBook>();
    snapshot_.reset();
    previous_.reset();
    snapshot_time_ = Timestamp{};
    stats_ = {};
    initialized_ = false;
    failed_ = false;
}
} // namespace microback
