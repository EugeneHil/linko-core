// GoogleTest suite for the protocol v1 typed message bodies (design spec 4.2).
// The test names are the contract from the task brief; keep them verbatim.
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "linko/envelope.hpp"
#include "linko/messages.hpp"

namespace {

constexpr std::int64_t kSampleTs = 1792877416123;
constexpr std::int64_t kSampleSeq = 7;
constexpr std::uint64_t kSampleFileSize = 4096;
constexpr std::uint32_t kSampleChunkSize = 16384;

// All errors of a parse formatted as "path: message" for failure messages.
auto joinedErrors(const std::vector<linko::ValidationError> &errors)
    -> std::string {
    std::string text;
    for (const auto &error : errors) {
        if (!text.empty()) {
            text += ", ";
        }
        text += error.path + ": " + error.message;
    }
    return text;
}

// The round-trip contract of test_all_bodies_roundtrip: parsing the body a
// struct builds yields the same struct and no errors.
template <typename T>
auto roundTrips(const T &value) -> testing::AssertionResult {
    std::vector<linko::ValidationError> errors;
    const auto parsed = T::fromBody(value.toBody(), errors);
    if (!errors.empty()) {
        return testing::AssertionFailure()
               << "unexpected errors: " << joinedErrors(errors);
    }
    if (!parsed.has_value()) {
        return testing::AssertionFailure() << "fromBody returned nullopt";
    }
    if (!(*parsed == value)) {
        return testing::AssertionFailure() << "parsed value differs";
    }
    return testing::AssertionSuccess();
}

// A minimal envelope of `type` with an empty body.
auto envelopeOfType(std::string_view type) -> std::string {
    return R"({"v":1,"type":")" + std::string(type) +
           R"(","id":"6f1c","ts":1792877416123,"body":{}})";
}

// An empty-body envelope of `type` parses cleanly, i.e. the type is
// registered in kKnownTypes.
auto parsesCleanly(std::string_view type) -> bool {
    const auto result = linko::parseEnvelope(envelopeOfType(type));
    return result.errors.empty() && result.envelope.has_value();
}

} // namespace

TEST(Messages, test_call_offer_body) {
    const linko::CallSdp offer{
        "c7a2",
        "v=0\r\no=- 46117317 2 IN IP4 127.0.0.1",
    };
    const auto body = offer.toBody();
    EXPECT_EQ(body["callId"], "c7a2");
    EXPECT_EQ(body["sdp"], "v=0\r\no=- 46117317 2 IN IP4 127.0.0.1");

    const linko::Envelope wireEnvelope{
        linko::kCurrentVersion,
        std::string(linko::kTypeCallOffer),
        "6f1c",
        kSampleTs,
        body,
    };
    const auto result = linko::parseEnvelope(linko::serialize(wireEnvelope));
    ASSERT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.envelope.has_value());
    const linko::Envelope &parsedEnvelope =
        result.envelope.value(); // NOLINT(bugprone-unchecked-optional-access)
    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::CallSdp::fromBody(parsedEnvelope.body, errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed == offer); // NOLINT(bugprone-unchecked-optional-access)
}

TEST(Messages, test_missing_required_field_path) {
    std::vector<linko::ValidationError> errors;
    const auto parsed =
        linko::CallSdp::fromBody(nlohmann::json{{"callId", "c7a2"}}, errors);
    EXPECT_FALSE(parsed.has_value());
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].path, "body.sdp");
    EXPECT_EQ(errors[0].message, "missing");
}

TEST(Messages, test_rejects_oversized_sdp) {
    const std::string oversized(linko::kMaxSdpBytes + 1, 'v');
    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::CallSdp::fromBody(
        nlohmann::json{{"callId", "c7a2"}, {"sdp", oversized}}, errors);
    EXPECT_FALSE(parsed.has_value());
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].path, "body.sdp");
    EXPECT_EQ(errors[0].message, "too large");

    // Exactly at the 32 KiB limit the SDP still parses.
    std::vector<linko::ValidationError> atLimitErrors;
    const auto atLimit = linko::CallSdp::fromBody(
        nlohmann::json{
            {"callId", "c7a2"},
            {"sdp", std::string(linko::kMaxSdpBytes, 'v')},
        },
        atLimitErrors);
    EXPECT_TRUE(atLimitErrors.empty());
    ASSERT_TRUE(atLimit.has_value());
}

TEST(Messages, test_invalid_media_rejected) {
    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::CallInvite::fromBody(
        nlohmann::json{{"callId", "c7a2"}, {"to", "anna"}, {"media", "radio"}},
        errors);
    EXPECT_FALSE(parsed.has_value());
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].path, "body.media");
    EXPECT_EQ(errors[0].message, "invalid");
}

TEST(Messages, test_msg_send_with_file_meta) {
    const linko::FileMeta meta{
        "report.pdf", 4096, "9f2c8ab34e5d", "p2p", "file-7",
    };
    const linko::MsgSend send{"m-1", "anna", "here is the report", meta};
    const auto body = send.toBody();
    ASSERT_TRUE(body["file"].is_object());
    EXPECT_EQ(body["file"]["name"], "report.pdf");
    EXPECT_EQ(body["file"]["size"], 4096);
    EXPECT_EQ(body["file"]["sha256"], "9f2c8ab34e5d");
    EXPECT_EQ(body["file"]["transport"], "p2p");
    EXPECT_EQ(body["file"]["fileId"], "file-7");

    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::MsgSend::fromBody(body, errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(*parsed == send); // NOLINT(bugprone-unchecked-optional-access)
}

TEST(Messages, test_msg_ack_roundtrip) {
    const linko::MsgAck ack{"conv-1", 41};
    const auto body = ack.toBody();
    EXPECT_EQ(body["conversation"], "conv-1");
    EXPECT_EQ(body["uptoSeq"], 41);

    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::MsgAck::fromBody(body, errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_TRUE(parsed.has_value());
    // The accesses below are guarded by the ASSERT above; tidy cannot see
    // through the gtest macro.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_EQ(parsed->conversation, "conv-1");
    EXPECT_EQ(parsed->uptoSeq, 41);
    EXPECT_TRUE(*parsed == ack);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(Messages, test_optional_reason_defaults) {
    // A missing reason parses as nullopt...
    std::vector<linko::ValidationError> errors;
    const auto parsed =
        linko::CallEnd::fromBody(nlohmann::json{{"callId", "c7a2"}}, errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_TRUE(parsed.has_value());
    // The accesses below are guarded by the ASSERT above; tidy cannot see
    // through the gtest macro.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_FALSE(parsed->reason.has_value());
    // ...and toBody omits the key entirely.
    EXPECT_FALSE(parsed->toBody().contains("reason"));
    // NOLINTEND(bugprone-unchecked-optional-access)

    // A present reason round-trips.
    const linko::CallEnd ended{"c7a2", std::optional<std::string>{"cancelled"}};
    std::vector<linko::ValidationError> roundtripErrors;
    const auto roundtripped =
        linko::CallEnd::fromBody(ended.toBody(), roundtripErrors);
    EXPECT_TRUE(roundtripErrors.empty());
    ASSERT_TRUE(roundtripped.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_EQ(*roundtripped->reason, "cancelled");
    EXPECT_TRUE(*roundtripped == ended);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(Messages, test_unknown_type_now_rejected) {
    // Every family type is registered in kKnownTypes and accepted.
    for (const auto type : linko::kKnownTypes) {
        EXPECT_TRUE(parsesCleanly(type)) << "type: " << type;
    }

    // Unknown types are still rejected.
    const auto rejected = linko::parseEnvelope(envelopeOfType("chat.say"));
    EXPECT_FALSE(rejected.envelope.has_value());
    ASSERT_EQ(rejected.errors.size(), 1U);
    EXPECT_EQ(rejected.errors[0].path, "type");
    EXPECT_EQ(rejected.errors[0].message, "unknown 'chat.say'");
}

TEST(Messages, test_file_offer_body_shape) {
    // The manifest fields ride at the top level of the file.offer body; the
    // JSON shape is pinned here independently of the C++ FileManifest layout.
    const linko::FileOffer offer{
        "c7a2",
        "file-7",
        linko::FileManifest{"report.pdf", 4096, "9f2c8ab34e5d", 16384, 1},
    };
    const auto body = offer.toBody();
    EXPECT_EQ(body["callId"], "c7a2");
    EXPECT_EQ(body["fileId"], "file-7");
    EXPECT_EQ(body["name"], "report.pdf");
    EXPECT_EQ(body["size"], 4096);
    EXPECT_EQ(body["sha256"], "9f2c8ab34e5d");
    EXPECT_EQ(body["chunkSize"], 16384);
    EXPECT_EQ(body["chunkCount"], 1);

    std::vector<linko::ValidationError> errors;
    const auto parsed = linko::FileOffer::fromBody(body, errors);
    EXPECT_TRUE(errors.empty());
    ASSERT_TRUE(parsed.has_value());
    // The access below is guarded by the ASSERT above; tidy cannot see
    // through the gtest macro.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(parsed->manifest, offer.manifest);
    EXPECT_TRUE(*parsed == offer); // NOLINT(bugprone-unchecked-optional-access)
}

TEST(Messages, test_all_bodies_roundtrip) {
    const linko::FileMeta meta{
        "report.pdf", 4096, "9f2c8ab34e5d", "p2p", "file-7",
    };
    const linko::MessageRecord plainRecord{
        "m-1", "anna", "bob", "hello", std::nullopt, 7, kSampleTs,
    };
    const linko::MessageRecord fileRecord{
        "m-2", "bob", "anna", "here is the report", meta, 8, kSampleTs,
    };

    EXPECT_TRUE(roundTrips(linko::SessionHello{"anna", "iphone-15"}));
    EXPECT_TRUE(
        roundTrips(linko::SessionError{"unsupported_version", "v must be 1"}));
    EXPECT_TRUE(roundTrips(linko::PresenceChanged{"anna", true}));
    EXPECT_TRUE(roundTrips(linko::PresenceChanged{"anna", false}));
    EXPECT_TRUE(roundTrips(linko::CallInvite{"c7a2", "anna", "audio"}));
    EXPECT_TRUE(roundTrips(linko::CallInvite{"c7a2", "anna", "video"}));
    EXPECT_TRUE(roundTrips(linko::CallRef{"c7a2"}));
    EXPECT_TRUE(roundTrips(linko::CallSdp{"c7a2", "v=0"}));
    EXPECT_TRUE(roundTrips(linko::CallIce{
        "c7a2",
        "candidate:1 1 UDP 2130706431 192.168.1.4 61665 typ host",
        "0",
        0,
    }));
    EXPECT_TRUE(roundTrips(linko::CallEnd{"c7a2", std::nullopt}));
    EXPECT_TRUE(
        roundTrips(linko::CallEnd{"c7a2", std::optional<std::string>{"busy"}}));
    EXPECT_TRUE(roundTrips(meta));
    EXPECT_TRUE(roundTrips(plainRecord));
    EXPECT_TRUE(roundTrips(fileRecord));
    EXPECT_TRUE(
        roundTrips(linko::MsgSend{"m-1", "bob", "hello", std::nullopt}));
    EXPECT_TRUE(roundTrips(linko::MsgSend{"m-2", "anna", "the report", meta}));
    EXPECT_TRUE(roundTrips(linko::MsgStored{"m-1", 7, kSampleTs}));
    EXPECT_TRUE(roundTrips(linko::MsgDeliver{fileRecord}));
    EXPECT_TRUE(roundTrips(linko::MsgAck{"conv-1", 41}));
    EXPECT_TRUE(roundTrips(linko::MsgSync{"conv-1", 40}));
    EXPECT_TRUE(roundTrips(linko::FileOffer{
        "c7a2",
        "file-7",
        linko::FileManifest{"report.pdf", 4096, "9f2c8ab34e5d", 16384, 1},
    }));
    EXPECT_TRUE(roundTrips(linko::FileAccept{"c7a2", "file-7"}));
    EXPECT_TRUE(
        roundTrips(linko::FileCancel{"c7a2", "file-7", "user cancelled"}));
}

// --- error branches of the fromBody validators ---

// fromBody of a non-object is rejected with exactly one "body: must be an
// object" error, for every family and the nested FileMeta/MessageRecord.
template <typename T> auto rejectsNonObjectBody() -> void {
    std::vector<linko::ValidationError> errors;
    const auto parsed = T::fromBody(nlohmann::json::array(), errors);
    EXPECT_FALSE(parsed.has_value());
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].path, "body");
    EXPECT_EQ(errors[0].message, "must be an object");
}

// fromBody of `body` fails with exactly one `path: message` error.
template <typename T>
auto expectSingleError(const nlohmann::json &body, const std::string &path,
                       const std::string &message) -> void {
    std::vector<linko::ValidationError> errors;
    const auto parsed = T::fromBody(body, errors);
    EXPECT_FALSE(parsed.has_value());
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].path, path);
    EXPECT_EQ(errors[0].message, message);
}

// `body` with `key` set to `value` (wrong type on purpose).
auto withField(nlohmann::json body, const char *key, nlohmann::json value)
    -> nlohmann::json {
    body[key] = std::move(value);
    return body;
}

TEST(Messages, test_rejects_non_object_body) {
    rejectsNonObjectBody<linko::SessionHello>();
    rejectsNonObjectBody<linko::SessionError>();
    rejectsNonObjectBody<linko::PresenceChanged>();
    rejectsNonObjectBody<linko::CallInvite>();
    rejectsNonObjectBody<linko::CallRef>();
    rejectsNonObjectBody<linko::CallSdp>();
    rejectsNonObjectBody<linko::CallIce>();
    rejectsNonObjectBody<linko::CallEnd>();
    rejectsNonObjectBody<linko::FileMeta>();
    rejectsNonObjectBody<linko::MessageRecord>();
    rejectsNonObjectBody<linko::MsgSend>();
    rejectsNonObjectBody<linko::MsgStored>();
    rejectsNonObjectBody<linko::MsgDeliver>();
    rejectsNonObjectBody<linko::MsgAck>();
    rejectsNonObjectBody<linko::MsgSync>();
    rejectsNonObjectBody<linko::FileOffer>();
    rejectsNonObjectBody<linko::FileAccept>();
    rejectsNonObjectBody<linko::FileCancel>();
}

TEST(Messages, test_wrong_field_types_rejected) {
    // A string field holding a boolean.
    expectSingleError<linko::SessionHello>(
        withField(linko::SessionHello{"anna", "iphone-15"}.toBody(), "user",
                  false),
        "body.user", "must be a string");

    // The enum-like string field takes the same path through requireString.
    expectSingleError<linko::CallInvite>(
        withField(linko::CallInvite{"c7a2", "anna", "audio"}.toBody(), "media",
                  false),
        "body.media", "must be a string");

    // A boolean field holding a string.
    expectSingleError<linko::PresenceChanged>(
        withField(linko::PresenceChanged{"anna", true}.toBody(), "online",
                  "yes"),
        "body.online", "must be a boolean");

    // An integer field holding a string.
    expectSingleError<linko::MsgStored>(
        withField(linko::MsgStored{"m-1", kSampleSeq, kSampleTs}.toBody(),
                  "seq", "seven"),
        "body.seq", "must be an integer");

    // The optional reason must still be a string when present.
    expectSingleError<linko::CallEnd>(
        withField(
            linko::CallEnd{"c7a2", std::optional<std::string>{"busy"}}.toBody(),
            "reason", false),
        "body.reason", "must be a string");
}

TEST(Messages, test_missing_required_fields_rejected) {
    expectSingleError<linko::SessionHello>(
        nlohmann::json{{"device", "iphone-15"}}, "body.user", "missing");
    expectSingleError<linko::SessionError>(
        nlohmann::json{{"detail", "v must be 1"}}, "body.code", "missing");
    expectSingleError<linko::PresenceChanged>(nlohmann::json{{"user", "anna"}},
                                              "body.online", "missing");
    expectSingleError<linko::CallInvite>(
        nlohmann::json{{"to", "anna"}, {"media", "video"}}, "body.callId",
        "missing");
    expectSingleError<linko::CallRef>(nlohmann::json::object(), "body.callId",
                                      "missing");
    expectSingleError<linko::CallIce>(
        nlohmann::json{
            {"callId", "c7a2"},
            {"candidate", "candidate:1 1 UDP 2130706431 192.168.1.4 61665"},
            {"sdpMid", "0"},
        },
        "body.sdpMLineIndex", "missing");
    expectSingleError<linko::CallEnd>(nlohmann::json::object(), "body.callId",
                                      "missing");
    expectSingleError<linko::MsgSend>(
        nlohmann::json{{"id", "m-1"}, {"to", "anna"}}, "body.text", "missing");
    expectSingleError<linko::MsgStored>(
        nlohmann::json{{"id", "m-1"}, {"ts", kSampleTs}}, "body.seq",
        "missing");
    expectSingleError<linko::MsgAck>(nlohmann::json{{"conversation", "conv-1"}},
                                     "body.uptoSeq", "missing");
    expectSingleError<linko::MsgSync>(
        nlohmann::json{{"conversation", "conv-1"}}, "body.afterSeq", "missing");

    auto offer =
        linko::FileOffer{
            "c7a2",
            "file-7",
            linko::FileManifest{
                "report.pdf",
                kSampleFileSize,
                "9f2c8ab34e5d",
                kSampleChunkSize,
                1,
            },
        }
            .toBody();
    offer.erase("chunkSize");
    expectSingleError<linko::FileOffer>(offer, "body.chunkSize", "missing");

    expectSingleError<linko::FileAccept>(nlohmann::json{{"callId", "c7a2"}},
                                         "body.fileId", "missing");
    expectSingleError<linko::FileCancel>(
        nlohmann::json{{"callId", "c7a2"}, {"fileId", "file-7"}}, "body.reason",
        "missing");
}

TEST(Messages, test_nested_file_and_message_errors) {
    // A file field that is present but not an object.
    expectSingleError<linko::MsgSend>(
        nlohmann::json{
            {"id", "m-1"},
            {"to", "anna"},
            {"text", "hi"},
            {"file", nlohmann::json::array()},
        },
        "body.file", "must be an object");

    // A file meta object missing one of its required fields.
    auto meta =
        linko::FileMeta{
            "report.pdf", kSampleFileSize, "9f2c8ab34e5d", "p2p", "file-7",
        }
            .toBody();
    meta.erase("transport");
    expectSingleError<linko::MsgSend>(
        nlohmann::json{
            {"id", "m-1"},
            {"to", "anna"},
            {"text", "hi"},
            {"file", meta},
        },
        "body.file.transport", "missing");

    // msg.deliver without, or with a malformed, nested message record.
    expectSingleError<linko::MsgDeliver>(nlohmann::json::object(),
                                         "body.message", "missing");
    expectSingleError<linko::MsgDeliver>(nlohmann::json{{"message", "x"}},
                                         "body.message", "must be an object");

    auto record =
        linko::MessageRecord{
            "m-1", "anna", "bob", "hello", std::nullopt, kSampleSeq, kSampleTs,
        }
            .toBody();
    record.erase("seq");
    expectSingleError<linko::MsgDeliver>(nlohmann::json{{"message", record}},
                                         "body.message.seq", "missing");
}
