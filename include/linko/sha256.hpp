// SHA-256 (FIPS 180-4), vendored as a single self-contained implementation
// in src/sha256.cpp - deliberately no new dependency (ADR-0002's dependency
// set is GoogleTest + nlohmann/json only). Used by the file-transfer
// integrity check; verified against the FIPS 180-4 example digests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace linko {

// A SHA-256 digest is 32 raw bytes.
inline constexpr std::size_t kSha256DigestBytes = 32;

// A SHA-256 digest.
using Sha256Digest = std::array<std::uint8_t, kSha256DigestBytes>;

// Digest of `data`. Pure: no I/O, no global state, no exceptions.
[[nodiscard]] auto sha256(const std::vector<std::uint8_t> &data)
    -> Sha256Digest;

// Lowercase hexadecimal rendering of `digest` (64 characters).
[[nodiscard]] auto toHex(const Sha256Digest &digest) -> std::string;

// Digest of `data` as lowercase hex - the form FileManifest::sha256Hex and
// the wire protocol carry.
[[nodiscard]] auto sha256Hex(const std::vector<std::uint8_t> &data)
    -> std::string;

} // namespace linko
