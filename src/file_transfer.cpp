// File chunking and reassembly (design spec 4.2); see
// include/linko/file_transfer.hpp for the contract. Pure functions and value
// types only: framing never allocates beyond its output, reassembly never
// trusts the manifest's SHA-256 without recomputing it.
#include "linko/file_transfer.hpp"

#include "linko/sha256.hpp"

namespace linko {
namespace {

constexpr unsigned kBitsPerByte = 8;
constexpr std::size_t kIndexFieldBytes = 4;
constexpr std::size_t kLengthFieldBytes = 4;

// Byte offsets inside the chunk-frame header.
constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kFileIdOffset = 1;
constexpr std::size_t kIndexOffset = kFileIdOffset + kFileIdBytes;
constexpr std::size_t kLengthOffset = kIndexOffset + kIndexFieldBytes;
static_assert(kChunkFrameHeaderBytes == kLengthOffset + kLengthFieldBytes);

void appendUInt32BE(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (unsigned byte = 0; byte < kIndexFieldBytes; ++byte) {
        const auto shift =
            kBitsPerByte * static_cast<unsigned>(kIndexFieldBytes - 1U - byte);
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

auto readUInt32BE(const std::vector<std::uint8_t> &bytes, std::size_t offset)
    -> std::uint32_t {
    std::uint32_t word = 0;
    for (std::size_t i = 0; i < kIndexFieldBytes; ++i) {
        word = (word << kBitsPerByte) |
               static_cast<std::uint32_t>(bytes[offset + i]);
    }
    return word;
}

} // namespace

auto makeManifest(const std::string &name, std::uint64_t size,
                  const std::string &sha256Hex, std::uint32_t chunkSize)
    -> FileManifest {
    FileManifest manifest;
    manifest.name = name;
    manifest.size = size;
    manifest.sha256Hex = sha256Hex;
    manifest.chunkSize = chunkSize == 0 ? kDefaultChunkSize : chunkSize;
    // ceil(size / chunkSize) without the usual size + chunkSize - 1
    // addition, which would overflow near the top of uint64.
    manifest.chunkCount = static_cast<std::uint32_t>(
        (size / manifest.chunkSize) +
        ((size % manifest.chunkSize != 0) ? 1U : 0U));
    return manifest;
}

auto frameChunk(const std::string &fileId, std::uint32_t index,
                const std::vector<std::uint8_t> &payload)
    -> std::vector<std::uint8_t> {
    if (fileId.size() != kFileIdBytes) {
        return {};
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(kChunkFrameHeaderBytes + payload.size());
    frame.push_back(kChunkFrameVersion);
    frame.insert(frame.end(), fileId.begin(), fileId.end());
    appendUInt32BE(frame, index);
    appendUInt32BE(frame, static_cast<std::uint32_t>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

auto parseChunkFrame(const std::vector<std::uint8_t> &frame)
    -> std::optional<ChunkFrame> {
    if (frame.size() < kChunkFrameHeaderBytes) {
        return std::nullopt;
    }
    if (frame[kVersionOffset] != kChunkFrameVersion) {
        return std::nullopt;
    }
    const std::uint32_t index = readUInt32BE(frame, kIndexOffset);
    const std::uint32_t length = readUInt32BE(frame, kLengthOffset);
    if (frame.size() - kChunkFrameHeaderBytes != length) {
        return std::nullopt;
    }

    ChunkFrame parsed;
    parsed.fileId.assign(
        frame.begin() + static_cast<std::ptrdiff_t>(kFileIdOffset),
        frame.begin() +
            static_cast<std::ptrdiff_t>(kFileIdOffset + kFileIdBytes));
    parsed.index = index;
    parsed.payload.assign(
        frame.begin() + static_cast<std::ptrdiff_t>(kChunkFrameHeaderBytes),
        frame.end());
    return parsed;
}

Reassembler::Reassembler(FileManifest manifest)
    : manifest_(std::move(manifest)) {}

auto Reassembler::expectedChunkSize(std::uint32_t index) const
    -> std::uint64_t {
    // Called only with index < chunkCount, so chunkCount >= 1 here.
    if (index == manifest_.chunkCount - 1) {
        return manifest_.size -
               (static_cast<std::uint64_t>(index) * manifest_.chunkSize);
    }
    return manifest_.chunkSize;
}

auto Reassembler::addChunk(std::uint32_t index,
                           const std::vector<std::uint8_t> &payload)
    -> AddResult {
    if (index >= manifest_.chunkCount) {
        return AddResult::Unexpected;
    }
    if (chunks_.find(index) != chunks_.end()) {
        return AddResult::Duplicate;
    }
    if (payload.size() != expectedChunkSize(index)) {
        return AddResult::Unexpected;
    }
    chunks_.emplace(index, payload);
    receivedBytes_ += payload.size();
    return AddResult::Accepted;
}

auto Reassembler::receivedBytes() const -> std::uint64_t {
    return receivedBytes_;
}

auto Reassembler::takeIfCompleteAndValid() const
    -> std::optional<std::vector<std::uint8_t>> {
    // addChunk stores only indices inside the manifest, so a map that holds
    // chunkCount entries covers exactly 0..chunkCount-1.
    if (chunks_.size() != static_cast<std::size_t>(manifest_.chunkCount)) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> file;
    file.reserve(static_cast<std::size_t>(manifest_.size));
    for (const auto &entry : chunks_) {
        file.insert(file.end(), entry.second.begin(), entry.second.end());
    }
    if (file.size() != manifest_.size) {
        return std::nullopt;
    }
    if (sha256Hex(file) != manifest_.sha256Hex) {
        return std::nullopt;
    }
    return file;
}

auto Reassembler::nextIndexNeeded() const -> std::uint32_t {
    std::uint32_t index = 0;
    while (index < manifest_.chunkCount &&
           chunks_.find(index) != chunks_.end()) {
        ++index;
    }
    return index;
}

void AckTracker::ack(std::uint32_t index) {
    if (index < nextToSend_) {
        return; // already covered by the resume point (duplicate ack)
    }
    if (index > nextToSend_) {
        ahead_.insert(index);
        return;
    }
    ++nextToSend_;
    while (!ahead_.empty() && *ahead_.begin() == nextToSend_) {
        ahead_.erase(ahead_.begin());
        ++nextToSend_;
    }
}

auto AckTracker::nextToSend() const -> std::uint32_t { return nextToSend_; }

} // namespace linko
