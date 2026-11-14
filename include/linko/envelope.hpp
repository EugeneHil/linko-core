// Protocol v1 envelope: parse and serialize (design spec 4.2).
//
// An envelope is a JSON object:
//   {"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{}}
// parseEnvelope checks size, strict UTF-8 and nesting depth on the raw bytes
// before any JSON parsing and reports every field-level problem with a JSON
// path; serialize turns an Envelope back into wire format.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace linko {

// Protocol limits and version (design spec 4.2).
inline constexpr int kCurrentVersion = 1;
inline constexpr std::size_t kMaxEnvelopeBytes = 65536; // 64 KiB
inline constexpr std::size_t kMaxSdpBytes = 32768;      // 32 KiB
inline constexpr int kMaxNestingDepth = 16;

// Message types the parser accepts. Currently the session family only; later
// tasks extend this set to all message families.
inline constexpr std::array<std::string_view, 4> kKnownTypes{
    "session.hello",
    "session.ping",
    "session.pong",
    "session.error",
};

// A decoded envelope. body stays raw JSON; typed structs per message family
// are layered on top in later tasks.
struct Envelope {
    int v;
    std::string type;
    std::string id;
    std::int64_t ts;
    nlohmann::json body;
};

// One validation problem; path is a JSON path such as "v" or "body.sdp".
struct ValidationError {
    std::string path;
    std::string message;
};

// Outcome of parseEnvelope. envelope is present only when errors is empty.
// unsupportedVersion is set for any v != kCurrentVersion so callers can
// answer with session.error{code:"unsupported_version"}.
struct ParseResult {
    std::optional<Envelope> envelope;
    std::vector<ValidationError> errors;
    bool unsupportedVersion = false;
};

// Parses and validates a wire-format envelope. Pure function: no I/O, and
// no exceptions escape (a parse failure becomes an error entry).
auto parseEnvelope(std::string_view json) -> ParseResult;

// Serializes an envelope to wire format.
auto serialize(const Envelope &envelope) -> std::string;

} // namespace linko
