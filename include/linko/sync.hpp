// Message sync helpers (design spec 4.2, "Message sync rules"):
// ConversationHistory holds the messages of one conversation ordered by the
// server-assigned, monotonically increasing seq, makes sends idempotent via
// the client-generated message ids, answers "everything after seq N" queries
// and tracks receipts as monotonic watermarks (read implies delivered,
// receipts never regress). Pure value types - no I/O, no clocks, no
// exceptions.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "linko/messages.hpp"

namespace linko {

// Receipt snapshot for one message, derived from the conversation's
// delivered/read watermarks.
struct ReceiptState {
    bool delivered = false;
    bool read = false;
};

// Why the most recent add() returned false (None after a successful add, and
// for a fresh history). add() itself stays a plain bool per the interface;
// this is the simple, documented way to tell its two rejections apart.
enum class AddError : std::uint8_t {
    None,
    DuplicateId, // id already stored; the first record wins
    InvalidSeq,  // seq <= 0; a server seq starts at 1
};

// The messages of one conversation plus its receipt watermarks.
class ConversationHistory {
  public:
    // Stores `record`, keeping the conversation ordered by seq. Returns
    // false - and stores nothing - when a record with the same id is already
    // present (idempotent sends: the first record wins) or when
    // record.seq <= 0; lastError() says which.
    auto add(MessageRecord record) -> bool;

    // Why the most recent add() returned false.
    [[nodiscard]] auto lastError() const -> AddError;

    // True when something is known to be missing above `seq`: the records
    // above `seq` do not form the contiguous run seq+1, seq+2, ... up to the
    // highest seq seen. True for 1,2,4 (3 is missing); false for a
    // contiguous history, for an empty history and when nothing is stored
    // above `seq`. The sync host uses this to decide it missed messages and
    // must re-issue "everything after seq N".
    [[nodiscard]] auto hasGapAfter(std::int64_t seq) const -> bool;

    // The records with seq > `seq`, ordered by seq - the reply to an
    // "everything after seq N" sync request.
    [[nodiscard]] auto afterSeq(std::int64_t seq) const
        -> std::vector<MessageRecord>;

    // Advances the delivered watermark to `uptoSeq`. Lower values are
    // ignored (receipts never regress).
    void markDelivered(std::int64_t uptoSeq);

    // Advances the read watermark to `uptoSeq` and, because reading implies
    // delivery, the delivered watermark with it. Lower values are ignored.
    void markRead(std::int64_t uptoSeq);

    // Receipt snapshot for the stored message with id `messageId`;
    // std::nullopt when no such message exists.
    [[nodiscard]] auto receiptFor(const std::string &messageId) const
        -> std::optional<ReceiptState>;

  private:
    std::vector<MessageRecord> records_; // ordered by seq
    std::unordered_set<std::string> ids_;
    AddError lastError_ = AddError::None;
    std::int64_t deliveredUpto_ = 0;
    std::int64_t readUpto_ = 0;
};

} // namespace linko
