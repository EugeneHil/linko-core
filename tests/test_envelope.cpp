// GoogleTest suite for the protocol v1 envelope (design spec 4.2).
// The test names are the contract from the task brief; keep them verbatim.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "linko/envelope.hpp"

namespace {

// All errors of a result formatted as "path: message" for easy assertions.
auto errorTexts(const linko::ParseResult &result) -> std::vector<std::string> {
    std::vector<std::string> texts;
    texts.reserve(result.errors.size());
    for (const auto &error : result.errors) {
        texts.push_back(error.path + ": " + error.message);
    }
    return texts;
}

auto hasError(const linko::ParseResult &result, const std::string &text)
    -> bool {
    const auto texts = errorTexts(result);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
}

// A valid envelope whose body nests objects `levels - 1` deep; the root
// object itself counts as level 1.
auto envelopeWithNesting(int levels) -> std::string {
    std::string json =
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":1,"body":)";
    for (int level = 1; level < levels; ++level) {
        json += "{\"a\":";
    }
    json += '1';
    for (int level = 1; level < levels; ++level) {
        json += '}';
    }
    json += '}'; // close the root object
    return json;
}

constexpr std::int64_t kSampleTs = 1792877416123;

} // namespace

TEST(Envelope, test_parses_minimal_envelope) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{}})");
    ASSERT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.envelope.has_value());
    // The ASSERT above already guarantees the optional is engaged; tidy
    // cannot see through the gtest macro.
    const linko::Envelope &envelope =
        result.envelope.value(); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(envelope.v, 1);
    EXPECT_EQ(envelope.type, "session.ping");
    EXPECT_EQ(envelope.id, "6f1c");
    EXPECT_EQ(envelope.ts, kSampleTs);
    EXPECT_TRUE(envelope.body.is_object());
    EXPECT_TRUE(envelope.body.empty());
}

TEST(Envelope, test_missing_fields_carry_paths) {
    const auto result = linko::parseEnvelope(R"({"body":{}})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_EQ(result.errors.size(), 4U);
    EXPECT_TRUE(hasError(result, "v: missing"));
    EXPECT_TRUE(hasError(result, "type: missing"));
    EXPECT_TRUE(hasError(result, "id: missing"));
    EXPECT_TRUE(hasError(result, "ts: missing"));
}

TEST(Envelope, test_empty_id_rejected) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"","ts":1792877416123,"body":{}})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_TRUE(hasError(result, "id: empty"));
}

TEST(Envelope, test_unsupported_version_flagged) {
    const auto result = linko::parseEnvelope(
        R"({"v":2,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{}})");
    EXPECT_TRUE(result.unsupportedVersion);
    EXPECT_FALSE(result.envelope.has_value());
}

TEST(Envelope, test_unknown_field_ignored) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{"n":5},"extra":"x"})");
    ASSERT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.envelope.has_value());
    const linko::Envelope &envelope =
        result.envelope.value(); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(envelope.body["n"], 5);
}

TEST(Envelope, test_unknown_type_rejected) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"chat.say","id":"6f1c","ts":1792877416123,"body":{}})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_TRUE(hasError(result, "type: unknown 'chat.say'"));
}

TEST(Envelope, test_body_must_be_object) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":[1,2]})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_TRUE(hasError(result, "body: must be an object"));
}

TEST(Envelope, test_rejects_invalid_utf8) {
    // 0xC3 0x28 inside a JSON string: a lead byte followed by '(' instead of a
    // continuation byte. nlohmann/json tolerates such bytes, so the envelope
    // parser must reject them before parsing.
    const std::string json =
        std::string(R"({"v":1,"type":"session.ping","id":"6)") + "\xC3\x28" +
        R"(","ts":1792877416123,"body":{}})";
    const auto result = linko::parseEnvelope(json);
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_FALSE(result.errors.empty());
}

TEST(Envelope, test_rejects_deep_nesting) {
    const auto tooDeep =
        linko::parseEnvelope(envelopeWithNesting(linko::kMaxNestingDepth + 1));
    EXPECT_FALSE(tooDeep.envelope.has_value());
    EXPECT_FALSE(tooDeep.errors.empty());

    const auto atLimit =
        linko::parseEnvelope(envelopeWithNesting(linko::kMaxNestingDepth));
    ASSERT_TRUE(atLimit.errors.empty());
    EXPECT_TRUE(atLimit.envelope.has_value());
}

TEST(Envelope, test_rejects_oversized_envelope) {
    const std::string suffix = R"("}})";
    // A valid envelope padded to exactly `total` bytes.
    const auto envelopeOfSize = [&suffix](std::size_t total) -> std::string {
        std::string json =
            R"({"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{"pad":")";
        json += std::string(total - json.size() - suffix.size(), 'a');
        return json + suffix;
    };

    const auto over =
        linko::parseEnvelope(envelopeOfSize(linko::kMaxEnvelopeBytes + 1));
    EXPECT_FALSE(over.envelope.has_value());
    EXPECT_FALSE(over.errors.empty());

    const auto atLimit =
        linko::parseEnvelope(envelopeOfSize(linko::kMaxEnvelopeBytes));
    EXPECT_TRUE(atLimit.errors.empty());
    EXPECT_TRUE(atLimit.envelope.has_value());
}

TEST(Envelope, test_rejects_non_object_root) {
    for (const auto &json :
         {std::string("[1,2,3]"), std::string("\"hi\""), std::string("42")}) {
        const auto result = linko::parseEnvelope(json);
        EXPECT_FALSE(result.envelope.has_value());
        EXPECT_FALSE(result.errors.empty());
    }
}

TEST(Envelope, test_serialize_roundtrip) {
    const linko::Envelope original{
        1,
        "session.ping",
        "6f1c",
        kSampleTs,
        nlohmann::json{{"seq", 7}, {"text", "hello"}},
    };
    const auto result = linko::parseEnvelope(linko::serialize(original));
    ASSERT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.envelope.has_value());
    const linko::Envelope &envelope =
        result.envelope.value(); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(envelope.v, original.v);
    EXPECT_EQ(envelope.type, original.type);
    EXPECT_EQ(envelope.id, original.id);
    EXPECT_EQ(envelope.ts, original.ts);
    EXPECT_EQ(envelope.body, original.body);
}

// --- byte-level and field-level error branches ---

// An envelope whose id embeds `idBytes` must be rejected as invalid UTF-8.
auto rejectsIdBytes(const std::string &idBytes) -> void {
    const std::string json = R"({"v":1,"type":"session.ping","id":"6)" +
                             idBytes + R"(","ts":1,"body":{}})";
    const auto result = linko::parseEnvelope(json);
    EXPECT_FALSE(result.envelope.has_value());
    ASSERT_EQ(result.errors.size(), 1U);
    EXPECT_EQ(result.errors[0].path, "");
    EXPECT_EQ(result.errors[0].message, "invalid UTF-8");
}

TEST(Envelope, test_accepts_multibyte_utf8) {
    // A 3-byte (Euro sign) and a 4-byte (emoji) sequence are valid UTF-8 and
    // must pass the byte-level gate before JSON parsing.
    const std::string multibyteId = "6\xE2\x82\xAC\xF0\x9F\x98\x80";
    const std::string json = R"({"v":1,"type":"session.ping","id":")" +
                             multibyteId + R"(","ts":1,"body":{}})";
    const auto result = linko::parseEnvelope(json);
    ASSERT_TRUE(result.errors.empty());
    ASSERT_TRUE(result.envelope.has_value());
    const linko::Envelope &envelope =
        result.envelope.value(); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(envelope.id, multibyteId);
}

TEST(Envelope, test_rejects_malformed_utf8_sequences) {
    rejectsIdBytes(std::string("\xFF"));         // never a lead byte
    rejectsIdBytes(std::string("\xE2\x82"));     // truncated inside the input
    rejectsIdBytes(std::string("\xC0\x80"));     // overlong encoding of U+0000
    rejectsIdBytes(std::string("\xED\xA0\x80")); // surrogate U+D800
    rejectsIdBytes("\xF4\x90\x80\x80");          // above U+10FFFF

    // A sequence that would run past the very end of the input.
    const auto truncated = linko::parseEnvelope(
        std::string(
            R"({"v":1,"type":"session.ping","id":"6f1c","ts":1,"body":{}})") +
        "\xE2\x82");
    EXPECT_FALSE(truncated.envelope.has_value());
    ASSERT_EQ(truncated.errors.size(), 1U);
    EXPECT_EQ(truncated.errors[0].message, "invalid UTF-8");
}

TEST(Envelope, test_rejects_invalid_json) {
    // Well-formed UTF-8 and nesting, but not parseable JSON.
    const auto result = linko::parseEnvelope(R"({"v":1,"type":)");
    EXPECT_FALSE(result.envelope.has_value());
    ASSERT_EQ(result.errors.size(), 1U);
    EXPECT_EQ(result.errors[0].path, "");
    EXPECT_NE(result.errors[0].message.find("invalid JSON"), std::string::npos);
}

TEST(Envelope, test_wrong_field_types_carry_paths) {
    const auto result =
        linko::parseEnvelope(R"({"v":"1","type":5,"id":7,"ts":"x","body":{}})");
    EXPECT_FALSE(result.envelope.has_value());
    ASSERT_EQ(result.errors.size(), 4U);
    EXPECT_TRUE(hasError(result, "v: must be an integer"));
    EXPECT_TRUE(hasError(result, "type: must be a string"));
    EXPECT_TRUE(hasError(result, "id: must be a string"));
    EXPECT_TRUE(hasError(result, "ts: must be an integer"));
}

TEST(Envelope, test_missing_body_rejected) {
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":1})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_TRUE(hasError(result, "body: missing"));
}

TEST(Envelope, test_rejects_out_of_range_ts) {
    // 2^64-1 parses as unsigned; it must be rejected, not wrapped by
    // get<int64_t>.
    const auto result = linko::parseEnvelope(
        R"({"v":1,"type":"session.ping","id":"6f1c","ts":18446744073709551615,"body":{}})");
    EXPECT_FALSE(result.envelope.has_value());
    EXPECT_TRUE(hasError(result, "ts: out of range"));
    EXPECT_FALSE(result.unsupportedVersion);
}
