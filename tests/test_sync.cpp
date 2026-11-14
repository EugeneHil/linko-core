// GoogleTest suite for the message sync helpers (design spec 4.2, "Message
// sync rules"): ConversationHistory keeps one conversation ordered by the
// server-assigned seq, dedups on the client-generated message ids, answers
// "everything after seq N" queries and tracks receipts as monotonic
// watermarks. The test names are the contract from the task brief; keep them
// verbatim. The suite is registered as the `sync` executable, so
// `ctest -R sync` runs exactly these scenarios.
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "linko/messages.hpp"
#include "linko/sync.hpp"

namespace {

using linko::AddError;
using linko::ConversationHistory;
using linko::MessageRecord;
using linko::ReceiptState;

// Sample timestamp for every record, and the seq of the top message in the
// five-message histories below.
constexpr std::int64_t kSampleTs = 1792877416123;
constexpr std::int64_t kTopSeq = 5;

// A stored message with id `messageId`, server seq `seq` and text `text`,
// from the sample sender to the sample recipient.
auto msg(std::string messageId, std::int64_t seq, std::string text)
    -> MessageRecord {
    MessageRecord record;
    record.id = std::move(messageId);
    record.from = "anna";
    record.to = "ben";
    record.text = std::move(text);
    record.seq = seq;
    record.ts = kSampleTs;
    return record;
}

// Same with the text defaulted, so every record reads back distinct.
auto msg(std::string messageId, std::int64_t seq) -> MessageRecord {
    return msg(std::move(messageId), seq, "m" + std::to_string(seq));
}

// The ids of `records`, in stored order.
auto ids(const std::vector<MessageRecord> &records)
    -> std::vector<std::string> {
    std::vector<std::string> result;
    result.reserve(records.size());
    for (const MessageRecord &record : records) {
        result.push_back(record.id);
    }
    return result;
}

// Expects the receipt of `messageId` to be present with exactly these flags.
void expectReceipt(const ConversationHistory &history,
                   const std::string &messageId, bool delivered, bool read) {
    const std::optional<ReceiptState> receipt = history.receiptFor(messageId);
    ASSERT_TRUE(receipt.has_value());
    const ReceiptState &state =
        receipt.value(); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(state.delivered, delivered);
    EXPECT_EQ(state.read, read);
}

TEST(sync, test_add_orders_by_seq) {
    ConversationHistory history;
    ASSERT_TRUE(history.add(msg("c", 3)));
    ASSERT_TRUE(history.add(msg("a", 1)));
    ASSERT_TRUE(history.add(msg("b", 2)));

    EXPECT_EQ(ids(history.afterSeq(0)),
              std::vector<std::string>({"a", "b", "c"}));
}

TEST(sync, test_duplicate_id_ignored) {
    ConversationHistory history;
    ASSERT_TRUE(history.add(msg("a", 1, "first")));

    EXPECT_FALSE(history.add(msg("a", 2, "second")));
    EXPECT_EQ(history.lastError(), AddError::DuplicateId);

    const std::vector<MessageRecord> all = history.afterSeq(0);
    ASSERT_EQ(all.size(), 1U);
    EXPECT_EQ(all[0].seq, 1);
    EXPECT_EQ(all[0].text, "first"); // the first record wins
}

TEST(sync, test_invalid_seq_rejected) {
    ConversationHistory history;

    EXPECT_FALSE(history.add(msg("zero", 0)));
    EXPECT_EQ(history.lastError(), AddError::InvalidSeq);
    EXPECT_FALSE(history.add(msg("negative", -2)));
    EXPECT_EQ(history.lastError(), AddError::InvalidSeq);
    EXPECT_TRUE(history.afterSeq(0).empty());

    ASSERT_TRUE(history.add(msg("ok", 1)));
    EXPECT_EQ(history.lastError(), AddError::None);
}

TEST(sync, test_seq_gap_detected) {
    ConversationHistory gapped;
    ASSERT_TRUE(gapped.add(msg("m1", 1)));
    ASSERT_TRUE(gapped.add(msg("m2", 2)));
    ASSERT_TRUE(gapped.add(msg("m4", 4)));

    EXPECT_TRUE(gapped.hasGapAfter(0));  // 3 is missing between 2 and 4
    EXPECT_TRUE(gapped.hasGapAfter(2));  // 4 is seen while 3 is not
    EXPECT_FALSE(gapped.hasGapAfter(3)); // nothing known missing above 3
    EXPECT_FALSE(gapped.hasGapAfter(4)); // nothing stored above the top

    ConversationHistory full;
    for (std::int64_t seq = 1; seq <= 3; ++seq) {
        ASSERT_TRUE(full.add(msg("m" + std::to_string(seq), seq)));
    }
    EXPECT_FALSE(full.hasGapAfter(0));

    const ConversationHistory empty;
    EXPECT_FALSE(empty.hasGapAfter(0));
}

TEST(sync, test_after_seq_returns_ordered_tail) {
    ConversationHistory history;
    for (std::int64_t seq = 1; seq <= kTopSeq; ++seq) {
        ASSERT_TRUE(history.add(msg("m" + std::to_string(seq), seq)));
    }

    EXPECT_EQ(ids(history.afterSeq(2)),
              std::vector<std::string>({"m3", "m4", "m5"}));
    EXPECT_EQ(ids(history.afterSeq(4)), std::vector<std::string>({"m5"}));
    EXPECT_TRUE(history.afterSeq(kTopSeq).empty());
    EXPECT_EQ(ids(history.afterSeq(0)),
              std::vector<std::string>({"m1", "m2", "m3", "m4", "m5"}));
}

TEST(sync, test_receipts_advance) {
    ConversationHistory history;
    ASSERT_TRUE(history.add(msg("m1", 1)));
    ASSERT_TRUE(history.add(msg("m2", 2)));
    ASSERT_TRUE(history.add(msg("m3", 3)));

    history.markDelivered(1);
    expectReceipt(history, "m1", true, false);
    expectReceipt(history, "m2", false, false);

    history.markDelivered(2);
    expectReceipt(history, "m2", true, false);
    expectReceipt(history, "m3", false, false);
}

TEST(sync, test_read_implies_delivered) {
    ConversationHistory history;
    ASSERT_TRUE(history.add(msg("m1", 1)));
    ASSERT_TRUE(history.add(msg("m2", 2)));

    history.markRead(1);
    expectReceipt(history, "m1", true, true); // reading implies delivery
    expectReceipt(history, "m2", false, false);
}

TEST(sync, test_receipts_never_regress) {
    ConversationHistory history;
    for (std::int64_t seq = 1; seq <= kTopSeq; ++seq) {
        ASSERT_TRUE(history.add(msg("m" + std::to_string(seq), seq)));
    }

    history.markRead(kTopSeq);
    history.markDelivered(3); // a lower watermark must not drag receipts back
    expectReceipt(history, "m4", true, true); // delivered stays at the top

    history.markRead(2); // a lower read watermark is ignored too
    expectReceipt(history, "m5", true, true);
}

TEST(sync, test_receipt_for_unknown_id_empty) {
    ConversationHistory history;
    ASSERT_TRUE(history.add(msg("m1", 1)));

    EXPECT_EQ(history.receiptFor("missing"), std::nullopt);
}

} // namespace
