#include "linko/envelope.hpp"

#include "linko/messages.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace linko {
namespace {

// Strict UTF-8 validation (RFC 3629): rejects malformed sequences, overlong
// encodings, surrogates and code points above U+10FFFF. nlohmann/json
// accepts raw invalid bytes inside strings, so envelopes are checked on the
// raw bytes before parsing.
auto isValidUtf8(std::string_view text) -> bool {
    // Lead-byte classification and payload masks (RFC 3629).
    constexpr std::uint32_t kAsciiLimit = 0x80;
    constexpr std::uint32_t kTwoByteLeadMask = 0xE0;
    constexpr std::uint32_t kTwoByteLeadValue = 0xC0;
    constexpr std::uint32_t kTwoBytePayload = 0x1F;
    constexpr std::uint32_t kThreeByteLeadMask = 0xF0;
    constexpr std::uint32_t kThreeByteLeadValue = 0xE0;
    constexpr std::uint32_t kThreeBytePayload = 0x0F;
    constexpr std::uint32_t kFourByteLeadMask = 0xF8;
    constexpr std::uint32_t kFourByteLeadValue = 0xF0;
    constexpr std::uint32_t kFourBytePayload = 0x07;
    constexpr std::uint32_t kContinuationMask = 0xC0;
    constexpr std::uint32_t kContinuationValue = 0x80;
    constexpr std::uint32_t kContinuationPayload = 0x3F;
    constexpr std::uint32_t kContinuationBits = 6;
    // Smallest scalar value per sequence length: rejects overlong encodings.
    constexpr std::array<std::uint32_t, 5> kMinCodePoint{
        0, 0, 0x80, 0x800, 0x10000,
    };
    constexpr std::uint32_t kUnicodeMax = 0x10FFFF;
    constexpr std::uint32_t kSurrogateFirst = 0xD800;
    constexpr std::uint32_t kSurrogateLast = 0xDFFF;

    for (std::size_t i = 0; i < text.size();) {
        const auto lead =
            static_cast<std::uint32_t>(static_cast<unsigned char>(text[i]));
        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (lead < kAsciiLimit) {
            length = 1;
            codePoint = lead;
        } else if ((lead & kTwoByteLeadMask) == kTwoByteLeadValue) {
            length = 2;
            codePoint = lead & kTwoBytePayload;
        } else if ((lead & kThreeByteLeadMask) == kThreeByteLeadValue) {
            length = 3;
            codePoint = lead & kThreeBytePayload;
        } else if ((lead & kFourByteLeadMask) == kFourByteLeadValue) {
            length = 4;
            codePoint = lead & kFourBytePayload;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto cont = static_cast<std::uint32_t>(
                static_cast<unsigned char>(text[i + k]));
            if ((cont & kContinuationMask) != kContinuationValue) {
                return false;
            }
            codePoint = (codePoint << kContinuationBits) |
                        (cont & kContinuationPayload);
        }
        if (codePoint < kMinCodePoint[length] || codePoint > kUnicodeMax ||
            (codePoint >= kSurrogateFirst && codePoint <= kSurrogateLast)) {
            return false;
        }
        i += length;
    }
    return true;
}

// Deepest object/array nesting measured on the raw bytes, with the root
// counting as level 1. Runs before nlohmann's recursive parser so hostile
// depth cannot exhaust the stack.
auto maxNestingDepth(std::string_view text) -> int {
    int depth = 0;
    int maxDepth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char current = text[i];
        if (inString) {
            if (current == '\\') {
                ++i; // skip the escaped character
            } else if (current == '"') {
                inString = false;
            }
            continue;
        }
        if (current == '"') {
            inString = true;
        } else if (current == '{' || current == '[') {
            ++depth;
            maxDepth = std::max(depth, maxDepth);
        } else if (current == '}' || current == ']') {
            --depth;
        }
    }
    return maxDepth;
}

auto isKnownType(std::string_view type) -> bool {
    return std::find(kKnownTypes.begin(), kKnownTypes.end(), type) !=
           kKnownTypes.end();
}

} // namespace

auto parseEnvelope(std::string_view json) -> ParseResult {
    ParseResult result;

    // Pre-parse gates: size, strict UTF-8 and nesting depth on the raw bytes.
    if (json.size() > kMaxEnvelopeBytes) {
        const std::string message =
            "envelope exceeds " + std::to_string(kMaxEnvelopeBytes) + " bytes";
        result.errors.push_back({"", message});
        return result;
    }
    if (!isValidUtf8(json)) {
        result.errors.push_back({"", "invalid UTF-8"});
        return result;
    }
    if (maxNestingDepth(json) > kMaxNestingDepth) {
        const std::string message = "nesting deeper than " +
                                    std::to_string(kMaxNestingDepth) +
                                    " levels";
        result.errors.push_back({"", message});
        return result;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json.begin(), json.end());
    } catch (const nlohmann::json::parse_error &error) {
        result.errors.push_back(
            {"", std::string("invalid JSON: ") + error.what()});
        return result;
    }

    if (!root.is_object()) {
        result.errors.push_back({"", "root must be an object"});
        return result;
    }

    // Version.
    const auto versionField = root.find("v");
    if (versionField == root.end()) {
        result.errors.push_back({"v", "missing"});
    } else if (!versionField->is_number_integer()) {
        result.errors.push_back({"v", "must be an integer"});
    } else if (versionField->get<std::int64_t>() != kCurrentVersion) {
        result.unsupportedVersion = true;
        result.errors.push_back({"v", "unsupported version"});
    }

    // Type.
    const auto typeField = root.find("type");
    if (typeField == root.end()) {
        result.errors.push_back({"type", "missing"});
    } else if (!typeField->is_string()) {
        result.errors.push_back({"type", "must be a string"});
    } else if (!isKnownType(typeField->get<std::string>())) {
        result.errors.push_back(
            {"type", "unknown '" + typeField->get<std::string>() + "'"});
    }

    // Id.
    const auto idField = root.find("id");
    if (idField == root.end()) {
        result.errors.push_back({"id", "missing"});
    } else if (!idField->is_string()) {
        result.errors.push_back({"id", "must be a string"});
    } else if (idField->get<std::string>().empty()) {
        result.errors.push_back({"id", "empty"});
    }

    // Timestamp. Unsigned values above INT64_MAX are rejected instead of
    // silently wrapping in get<int64_t>.
    const auto tsField = root.find("ts");
    if (tsField == root.end()) {
        result.errors.push_back({"ts", "missing"});
    } else if (!tsField->is_number_integer()) {
        result.errors.push_back({"ts", "must be an integer"});
    } else if (tsField->is_number_unsigned() &&
               tsField->get<std::uint64_t>() >
                   static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max())) {
        result.errors.push_back({"ts", "out of range"});
    }

    // Body.
    const auto bodyField = root.find("body");
    if (bodyField == root.end()) {
        result.errors.push_back({"body", "missing"});
    } else if (!bodyField->is_object()) {
        result.errors.push_back({"body", "must be an object"});
    }

    if (!result.errors.empty()) {
        return result;
    }

    result.envelope = Envelope{
        static_cast<int>(versionField->get<std::int64_t>()),
        typeField->get<std::string>(),
        idField->get<std::string>(),
        tsField->get<std::int64_t>(),
        std::move(*bodyField),
    };
    return result;
}

auto serialize(const Envelope &envelope) -> std::string {
    nlohmann::json root;
    root["v"] = envelope.v;
    root["type"] = envelope.type;
    root["id"] = envelope.id;
    root["ts"] = envelope.ts;
    root["body"] = envelope.body;
    return root.dump();
}

} // namespace linko
