// File chunking and reassembly (design spec 4.2): the manifest, the wire
// framing of a single chunk and the reassembly state machine for in-call file
// transfers. Pure C++17 value types - no I/O, no clocks, no exceptions; the
// host moves the frames over the data channel and feeds them back here.
//
// Note: the task brief specifies std::span payload parameters; std::span is
// C++20, so this C++17 codebase takes `const std::vector<std::uint8_t>&`
// instead - the same contiguous-byte-view contract in the type the standard
// library of this project actually has.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace linko {

// Default payload bytes per chunk (design spec 4.2: 16 KiB).
inline constexpr std::uint32_t kDefaultChunkSize = 16 * 1024;

// Chunk-frame wire format (version 1): one version byte, the 16 raw fileId
// bytes, a 4-byte big-endian chunk index and a 4-byte big-endian payload
// length, followed by exactly that many payload bytes. A frame is rejected
// unless every announced byte is present.
inline constexpr std::uint8_t kChunkFrameVersion = 0x01;
inline constexpr std::size_t kFileIdBytes = 16;
inline constexpr std::size_t kChunkFrameHeaderBytes = 1 + kFileIdBytes + 4 + 4;

// What a file looks like before any byte moves. sha256Hex is the lowercase
// hex digest of the whole file, as produced by sha256Hex().
struct FileManifest {
    std::string name;
    std::uint64_t size = 0;
    std::string sha256Hex;
    std::uint32_t chunkSize = kDefaultChunkSize;
    std::uint32_t chunkCount = 0;

    auto operator==(const FileManifest &other) const -> bool {
        return std::tie(name, size, sha256Hex, chunkSize, chunkCount) ==
               std::tie(other.name, other.size, other.sha256Hex,
                        other.chunkSize, other.chunkCount);
    }
};

// Builds a manifest, computing chunkCount = ceil(size / chunkSize). An empty
// file yields chunkCount 0 - the manifest alone describes it completely. A
// chunkSize of 0 falls back to kDefaultChunkSize instead of dividing by zero.
[[nodiscard]] auto makeManifest(const std::string &name, std::uint64_t size,
                                const std::string &sha256Hex,
                                std::uint32_t chunkSize = kDefaultChunkSize)
    -> FileManifest;

// A parsed chunk frame; fileId is the raw 16 bytes from the wire.
struct ChunkFrame {
    std::string fileId;
    std::uint32_t index = 0;
    std::vector<std::uint8_t> payload;

    auto operator==(const ChunkFrame &other) const -> bool {
        return std::tie(fileId, index, payload) ==
               std::tie(other.fileId, other.index, other.payload);
    }
};

// Frames `payload` as chunk `index` of `fileId`. A fileId that is not 16
// bytes cannot be framed; the answer is an empty vector (a real frame is
// always at least the 25-byte header).
[[nodiscard]] auto frameChunk(const std::string &fileId, std::uint32_t index,
                              const std::vector<std::uint8_t> &payload)
    -> std::vector<std::uint8_t>;

// Parses a frame; nullopt for a wrong version byte, a truncated input
// (fewer than 25 header bytes), a length field that does not exactly match
// the remaining bytes, or - as a consequence of the fixed layout - any
// input too short to carry a 16-byte fileId.
[[nodiscard]] auto parseChunkFrame(const std::vector<std::uint8_t> &frame)
    -> std::optional<ChunkFrame>;

// Outcome of Reassembler::addChunk.
enum class AddResult : std::uint8_t {
    Accepted,
    Duplicate,
    Unexpected,
};

// Reassembles one file from its chunks. Chunks may arrive in any order; the
// two rules that can never be bent: a chunk is stored only when its index is
// inside the manifest and its size matches the manifest's expectation
// (kDefaultChunkSize, or the shorter last chunk), and the assembled bytes
// are handed out only when every index is present AND the SHA-256 of the
// assembly equals the manifest digest.
class Reassembler {
  public:
    explicit Reassembler(FileManifest manifest);

    // Stores chunk `index`. Duplicate: the index is already stored (the
    // stored bytes stand; nothing changes). Unexpected: the index is at or
    // beyond the manifest's chunkCount, or the payload size does not match
    // the manifest's expectation for that index.
    auto addChunk(std::uint32_t index, const std::vector<std::uint8_t> &payload)
        -> AddResult;

    // Payload bytes accepted so far; duplicates and rejects never count.
    [[nodiscard]] auto receivedBytes() const -> std::uint64_t;

    // The assembled file, but only when every chunk index is present and the
    // SHA-256 of the assembled bytes equals manifest.sha256Hex; nullopt
    // otherwise (incomplete or corrupt). Never returns wrong bytes.
    [[nodiscard]] auto takeIfCompleteAndValid() const
        -> std::optional<std::vector<std::uint8_t>>;

    // The lowest missing chunk index; equals chunkCount when nothing is
    // missing (an empty file's manifest starts out complete).
    [[nodiscard]] auto nextIndexNeeded() const -> std::uint32_t;

  private:
    // Payload size the manifest expects for `index`, which must be inside
    // the manifest.
    [[nodiscard]] auto expectedChunkSize(std::uint32_t index) const
        -> std::uint64_t;

    FileManifest manifest_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> chunks_;
    std::uint64_t receivedBytes_ = 0;
};

// Tracks which chunks the receiver has acknowledged, so a sender resuming
// after a drop continues from the resume point: the first chunk index that
// has not been acknowledged. For in-order acks that is "last acked + 1"; an
// ack ahead of a gap waits until the gap closes, so no chunk is skipped.
class AckTracker {
  public:
    // Records that the receiver has chunk `index`. Duplicate acks (at or
    // behind the resume point) change nothing.
    void ack(std::uint32_t index);

    // The resume point: the next chunk index to send.
    [[nodiscard]] auto nextToSend() const -> std::uint32_t;

  private:
    std::uint32_t nextToSend_ = 0;
    std::set<std::uint32_t> ahead_; // acked indices above the resume point
};

} // namespace linko
