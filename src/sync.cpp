// Implementation of the message sync helpers (design spec 4.2); the contract
// and invariants live in include/linko/sync.hpp.
#include "linko/sync.hpp"

#include <algorithm>
#include <limits>

namespace linko {

auto ConversationHistory::add(MessageRecord record) -> bool {
    if (record.seq <= 0) {
        lastError_ = AddError::InvalidSeq;
        return false;
    }
    if (ids_.find(record.id) != ids_.end()) {
        lastError_ = AddError::DuplicateId;
        return false;
    }

    ids_.insert(record.id);
    const auto position = std::lower_bound(
        records_.begin(), records_.end(), record.seq,
        [](const MessageRecord &stored, std::int64_t seq) -> bool {
            return stored.seq < seq;
        });
    records_.insert(position, std::move(record));
    lastError_ = AddError::None;
    return true;
}

auto ConversationHistory::lastError() const -> AddError { return lastError_; }

auto ConversationHistory::hasGapAfter(std::int64_t seq) const -> bool {
    if (records_.empty() || records_.back().seq <= seq) {
        return false; // nothing is stored above seq
    }

    // The records above `seq` must be exactly seq+1, seq+2, ... up to the
    // highest seq seen; the first break in that run is the hole.
    std::int64_t expected = seq + 1;
    for (const MessageRecord &record : records_) {
        if (record.seq <= seq) {
            continue;
        }
        if (record.seq != expected) {
            return true;
        }
        if (expected < std::numeric_limits<std::int64_t>::max()) {
            ++expected;
        }
    }
    return false;
}

auto ConversationHistory::afterSeq(std::int64_t seq) const
    -> std::vector<MessageRecord> {
    const auto first = std::upper_bound(
        records_.begin(), records_.end(), seq,
        [](std::int64_t value, const MessageRecord &record) -> bool {
            return value < record.seq;
        });
    return {first, records_.end()};
}

void ConversationHistory::markDelivered(std::int64_t uptoSeq) {
    deliveredUpto_ = std::max(deliveredUpto_, uptoSeq);
}

void ConversationHistory::markRead(std::int64_t uptoSeq) {
    readUpto_ = std::max(readUpto_, uptoSeq);
    // Reading implies delivery; both watermarks stay monotonic.
    deliveredUpto_ = std::max(deliveredUpto_, uptoSeq);
}

auto ConversationHistory::receiptFor(const std::string &messageId) const
    -> std::optional<ReceiptState> {
    const auto record =
        std::find_if(records_.begin(), records_.end(),
                     [&messageId](const MessageRecord &stored) -> bool {
                         return stored.id == messageId;
                     });
    if (record == records_.end()) {
        return std::nullopt;
    }

    ReceiptState state;
    state.delivered = record->seq <= deliveredUpto_;
    state.read = record->seq <= readUpto_;
    return state;
}

} // namespace linko
