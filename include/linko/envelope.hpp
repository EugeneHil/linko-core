// Protocol v1 envelope: parse and serialize (design spec 4.2).
//
// An envelope is a JSON object:
//   {"v":1,"type":"session.ping","id":"6f1c","ts":1792877416123,"body":{}}
// parseEnvelope checks size, strict UTF-8 and nesting depth on the raw bytes
// before any JSON parsing and reports every field-level problem with a JSON
// path; serialize turns an Envelope back into wire format.
#pragma once

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

// Accepted message types live in messages.hpp: the kType* name constants
// there are the single source of truth and build kKnownTypes.

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

// Serializes an envelope to wire format. Throws nlohmann::json::type_error
// 316 (invalid UTF-8) only when a hand-built Envelope puts invalid UTF-8
// into a string: dump() is strict. Envelopes that came out of parseEnvelope
// (which gates UTF-8 before parsing) and bodies built by the typed toBody()
// functions in messages.hpp are valid UTF-8 by construction, so serialize
// never throws for them.
auto serialize(const Envelope &envelope) -> std::string;

} // namespace linko
