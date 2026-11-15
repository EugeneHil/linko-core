// GoogleTest suite for file chunking and reassembly (design spec 4.2).
// The test names are the contract from the task brief; keep them verbatim.
// The suite name is lowercase so `ctest -R file_transfer` matches its tests.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "linko/file_transfer.hpp"
#include "linko/sha256.hpp"

namespace {

// A well-formed 16-byte file id for the framing tests.
constexpr const char *kFileId = "0123456789abcdef";

// SHA-256 known answers (FIPS 180-4 examples): digest of the empty string
// and of "abc".
constexpr const char *kEmptySha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr const char *kAbcSha256 =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

// Byte pattern of the fixtures: no two chunk slices of a fixture are equal.
constexpr unsigned kPatternFactor = 37;
constexpr unsigned kPatternOffset = 11;
constexpr unsigned kPatternModulus = 251;

// SHA-256 padding-edge sizes (64-byte compression block).
constexpr std::size_t kBlockSize64 = 64;
constexpr std::size_t kTail55 = 55;
constexpr std::size_t kTail56 = 56;
constexpr std::size_t kMultiBlockBytes = 130;

// Wire-layout helpers for the framing tests.
constexpr unsigned kBitsPerByte = 8;
constexpr std::size_t kIndexFieldBytes = 4;
constexpr std::size_t kFileIdOffset = 1;
constexpr std::size_t kIndexOffset = kFileIdOffset + linko::kFileIdBytes;
constexpr std::size_t kLengthOffset = kIndexOffset + kIndexFieldBytes;
constexpr std::uint32_t kSampleIndex = 7;
constexpr std::uint32_t kSamplePayloadBytes = 4;

// n payload bytes with a deterministic pattern.
auto patternBytes(std::size_t n) -> std::vector<std::uint8_t> {
    std::vector<std::uint8_t> bytes(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<std::uint8_t>(
            ((i * kPatternFactor) + kPatternOffset) % kPatternModulus);
    }
    return bytes;
}

auto slice(const std::vector<std::uint8_t> &data, std::size_t begin,
           std::size_t end) -> std::vector<std::uint8_t> {
    return {data.begin() + static_cast<std::ptrdiff_t>(begin),
            data.begin() + static_cast<std::ptrdiff_t>(end)};
}

// The three-chunk fixture of the reassembly tests: kFileSize bytes in
// kChunkSize-byte chunks, so the last chunk is the short one (kTailSize).
struct ThreeChunkFile {
    static constexpr std::uint32_t kChunkSize = 8;
    static constexpr std::size_t kFileSize = 20;
    static constexpr std::size_t kTailSize = 4;

    std::vector<std::uint8_t> data = patternBytes(kFileSize);
    linko::FileManifest manifest = linko::makeManifest(
        "photo.bin", kFileSize, linko::sha256Hex(data), kChunkSize);

    // Chunk `index` of the fixture file, at the size the manifest expects.
    [[nodiscard]] auto chunkOf(std::size_t index) const
        -> std::vector<std::uint8_t> {
        const std::size_t begin = index * kChunkSize;
        const std::size_t end =
            (begin + kChunkSize) > kFileSize ? kFileSize : begin + kChunkSize;
        return slice(data, begin, end);
    }
};

} // namespace

TEST(file_transfer, test_sha256_known_answers) {
    EXPECT_EQ(linko::sha256Hex({}), kEmptySha256);
    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    EXPECT_EQ(linko::sha256Hex(abc), kAbcSha256);

    // Padding edges beyond the two FIPS vectors (digests cross-checked
    // against Python's hashlib): an exact 64-byte block, the longest tail
    // (55 bytes) that still fits one padded block, a 56-byte tail that
    // forces the padding into a second block, and a multi-block input.
    const auto repeated = [](char byte,
                             std::size_t count) -> std::vector<std::uint8_t> {
        const std::vector<std::uint8_t> bytes(count,
                                              static_cast<std::uint8_t>(byte));
        return bytes;
    };
    EXPECT_EQ(
        linko::sha256Hex(repeated('a', kBlockSize64)),
        "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    EXPECT_EQ(
        linko::sha256Hex(repeated('b', kTail55)),
        "eb2c86e932179f4ba13fe8715a26124b77d6bad290b9b4c1cc140cf633300c19");
    EXPECT_EQ(
        linko::sha256Hex(repeated('c', kTail56)),
        "f6c7b87acd114115d66897c8cb138c16a8b886673d1b93737f4918be472ea878");
    EXPECT_EQ(
        linko::sha256Hex(patternBytes(kMultiBlockBytes)),
        "8df73e511f54cda3b0cd87bd99d68e51cf832f6b4030dadb547bf0bd89c5f929");
}

TEST(file_transfer, test_manifest_chunk_count_rounding) {
    const auto chunk = static_cast<std::uint64_t>(linko::kDefaultChunkSize);

    const auto empty = linko::makeManifest("f", 0, "00");
    EXPECT_EQ(empty.chunkCount, 0U);
    EXPECT_EQ(empty.chunkSize, linko::kDefaultChunkSize);

    const auto tiny = linko::makeManifest("f", 1, "00");
    EXPECT_EQ(tiny.chunkCount, 1U);

    const auto exact = linko::makeManifest("f", chunk, "00");
    EXPECT_EQ(exact.chunkCount, 1U);

    const auto oneOver = linko::makeManifest("f", chunk + 1, "00");
    EXPECT_EQ(oneOver.chunkCount, 2U);

    const auto threeExact = linko::makeManifest("f", 3 * chunk, "00");
    EXPECT_EQ(threeExact.chunkCount, 3U);

    const auto threeOver = linko::makeManifest("f", (3 * chunk) + 1, "00");
    EXPECT_EQ(threeOver.chunkCount, 4U);

    // An explicit chunk size overrides the default.
    constexpr std::uint32_t kSmallChunk = 8;
    const auto custom = linko::makeManifest("f", 17, "00", kSmallChunk);
    EXPECT_EQ(custom.chunkCount, 3U);
    EXPECT_EQ(custom.chunkSize, kSmallChunk);
    const auto customExact = linko::makeManifest("f", 16, "00", kSmallChunk);
    EXPECT_EQ(customExact.chunkCount, 2U);

    // A zero chunk size falls back to the default instead of dividing by
    // zero.
    const auto zeroChunk = linko::makeManifest("f", 1, "00", 0);
    EXPECT_EQ(zeroChunk.chunkSize, linko::kDefaultChunkSize);
    EXPECT_EQ(zeroChunk.chunkCount, 1U);
}

TEST(file_transfer, test_frame_roundtrip) {
    const std::vector<std::uint8_t> payload = patternBytes(kSamplePayloadBytes);
    const auto frame = linko::frameChunk(kFileId, kSampleIndex, payload);
    ASSERT_EQ(frame.size(), linko::kChunkFrameHeaderBytes + payload.size());

    // The wire layout itself: version byte, raw fileId, big-endian index,
    // then big-endian length.
    EXPECT_EQ(frame[0], linko::kChunkFrameVersion);
    EXPECT_EQ(
        std::string(frame.begin() + kFileIdOffset,
                    frame.begin() + (kFileIdOffset + linko::kFileIdBytes)),
        std::string(kFileId));
    EXPECT_EQ(frame[kIndexOffset],
              static_cast<std::uint8_t>(kSampleIndex >> (kBitsPerByte * 3)));
    EXPECT_EQ(frame[kIndexOffset + 1],
              static_cast<std::uint8_t>(kSampleIndex >> (kBitsPerByte * 2)));
    EXPECT_EQ(frame[kIndexOffset + 2],
              static_cast<std::uint8_t>(kSampleIndex >> kBitsPerByte));
    EXPECT_EQ(frame[kIndexOffset + 3], static_cast<std::uint8_t>(kSampleIndex));
    EXPECT_EQ(frame[kLengthOffset],
              static_cast<std::uint8_t>(payload.size() >> (kBitsPerByte * 3)));
    EXPECT_EQ(frame[kLengthOffset + 1],
              static_cast<std::uint8_t>(payload.size() >> (kBitsPerByte * 2)));
    EXPECT_EQ(frame[kLengthOffset + 2],
              static_cast<std::uint8_t>(payload.size() >> kBitsPerByte));
    EXPECT_EQ(frame[kLengthOffset + 3],
              static_cast<std::uint8_t>(payload.size()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
                           frame.begin() + linko::kChunkFrameHeaderBytes));

    const auto parsed = linko::parseChunkFrame(frame);
    ASSERT_TRUE(parsed.has_value());
    // The accesses below are guarded by the ASSERT above; tidy cannot see
    // through the gtest macro.
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_EQ(parsed->fileId, kFileId);
    EXPECT_EQ(parsed->index, kSampleIndex);
    EXPECT_EQ(parsed->payload, payload);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(file_transfer, test_frame_rejects_bad_version_and_truncation) {
    const auto frame =
        linko::frameChunk(kFileId, 0, patternBytes(kSamplePayloadBytes));
    ASSERT_FALSE(frame.empty());

    // Wrong version byte.
    auto badVersion = frame;
    badVersion[0] = 0x02;
    EXPECT_FALSE(linko::parseChunkFrame(badVersion).has_value());

    // Truncated input: one byte short of the header, and empty input.
    const std::vector<std::uint8_t> truncated(
        frame.begin(), frame.begin() + (linko::kChunkFrameHeaderBytes - 1));
    EXPECT_FALSE(linko::parseChunkFrame(truncated).has_value());
    EXPECT_FALSE(linko::parseChunkFrame({}).has_value());

    // Length field exceeding the remaining bytes.
    auto longLength = frame;
    longLength[(kLengthOffset + kIndexFieldBytes) - 1] =
        static_cast<std::uint8_t>(kSamplePayloadBytes + 1U);
    EXPECT_FALSE(linko::parseChunkFrame(longLength).has_value());

    // Trailing bytes after the announced payload are rejected too.
    auto trailing = frame;
    trailing.push_back(0x00);
    EXPECT_FALSE(linko::parseChunkFrame(trailing).has_value());

    // A fileId that is not 16 bytes cannot be framed: frameChunk answers with
    // an empty vector (a real frame is always at least the header).
    EXPECT_TRUE(linko::frameChunk("short-id", 0, {1}).empty());
}

TEST(file_transfer, test_reassembly_happy_path) {
    const ThreeChunkFile file;
    ASSERT_EQ(file.manifest.chunkCount, 3U);

    linko::Reassembler reassembler(file.manifest);
    EXPECT_EQ(reassembler.nextIndexNeeded(), 0U);
    EXPECT_EQ(reassembler.receivedBytes(), 0U);

    ASSERT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Accepted);
    EXPECT_EQ(reassembler.receivedBytes(), ThreeChunkFile::kChunkSize);
    EXPECT_EQ(reassembler.nextIndexNeeded(), 1U);

    ASSERT_EQ(reassembler.addChunk(1, file.chunkOf(1)),
              linko::AddResult::Accepted);
    EXPECT_EQ(reassembler.nextIndexNeeded(), 2U);

    ASSERT_EQ(reassembler.addChunk(2, file.chunkOf(2)),
              linko::AddResult::Accepted);
    EXPECT_EQ(reassembler.receivedBytes(), ThreeChunkFile::kFileSize);
    // Nothing is missing anymore: the resume cursor rests at chunkCount.
    EXPECT_EQ(reassembler.nextIndexNeeded(), file.manifest.chunkCount);

    const auto assembled = reassembler.takeIfCompleteAndValid();
    ASSERT_TRUE(assembled.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_EQ(*assembled, file.data);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(file_transfer, test_corrupt_payload_fails_integrity) {
    constexpr unsigned kCorruptFlipMask = 0xFF;

    const ThreeChunkFile file;
    linko::Reassembler reassembler(file.manifest);
    ASSERT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Accepted);
    ASSERT_EQ(reassembler.addChunk(2, file.chunkOf(2)),
              linko::AddResult::Accepted);

    // Chunk 1 with one flipped byte: still accepted (the size is right)...
    auto corrupt = file.chunkOf(1);
    const unsigned flipped =
        static_cast<unsigned>(corrupt[0]) ^ kCorruptFlipMask;
    corrupt[0] = static_cast<std::uint8_t>(flipped);
    ASSERT_EQ(reassembler.addChunk(1, corrupt), linko::AddResult::Accepted);
    EXPECT_EQ(reassembler.receivedBytes(), ThreeChunkFile::kFileSize);

    // ...but the integrity check refuses to hand out wrong bytes.
    EXPECT_FALSE(reassembler.takeIfCompleteAndValid().has_value());
}

TEST(file_transfer, test_duplicate_chunk_ignored) {
    const ThreeChunkFile file;
    linko::Reassembler reassembler(file.manifest);
    ASSERT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Accepted);
    EXPECT_EQ(reassembler.receivedBytes(), ThreeChunkFile::kChunkSize);

    EXPECT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Duplicate);
    EXPECT_EQ(reassembler.receivedBytes(), ThreeChunkFile::kChunkSize);
    EXPECT_EQ(reassembler.nextIndexNeeded(), 1U);
}

TEST(file_transfer, test_missing_chunk_never_completes) {
    const ThreeChunkFile file;
    linko::Reassembler reassembler(file.manifest);
    ASSERT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Accepted);
    ASSERT_EQ(reassembler.addChunk(2, file.chunkOf(2)),
              linko::AddResult::Accepted);

    EXPECT_EQ(reassembler.nextIndexNeeded(), 1U);
    EXPECT_FALSE(reassembler.takeIfCompleteAndValid().has_value());
}

TEST(file_transfer, test_out_of_range_chunk_unexpected) {
    const ThreeChunkFile file;
    linko::Reassembler reassembler(file.manifest);

    // Index at or beyond chunkCount.
    EXPECT_EQ(reassembler.addChunk(3, file.chunkOf(0)),
              linko::AddResult::Unexpected);
    EXPECT_EQ(reassembler.receivedBytes(), 0U);

    // Non-last chunk with the wrong size.
    EXPECT_EQ(reassembler.addChunk(0, patternBytes(ThreeChunkFile::kTailSize)),
              linko::AddResult::Unexpected);
    // Last chunk (index 2) expects the short tail size, not a full chunk.
    EXPECT_EQ(reassembler.addChunk(2, file.chunkOf(0)),
              linko::AddResult::Unexpected);
    EXPECT_EQ(reassembler.receivedBytes(), 0U);

    // A rejected chunk leaves the reassembler usable.
    EXPECT_EQ(reassembler.addChunk(0, file.chunkOf(0)),
              linko::AddResult::Accepted);
}

TEST(file_transfer, test_resume_from_last_acked) {
    linko::AckTracker acks;
    EXPECT_EQ(acks.nextToSend(), 0U);
    acks.ack(0);
    EXPECT_EQ(acks.nextToSend(), 1U);
    acks.ack(1);
    EXPECT_EQ(acks.nextToSend(), 2U);

    // An ack ahead of the gap does not advance the resume point; closing the
    // gap jumps over everything already acknowledged.
    acks.ack(3);
    EXPECT_EQ(acks.nextToSend(), 2U);
    acks.ack(2);
    EXPECT_EQ(acks.nextToSend(), 4U);

    // Duplicate acks never move the cursor backwards.
    acks.ack(1);
    EXPECT_EQ(acks.nextToSend(), 4U);
}

TEST(file_transfer, test_empty_file_single_manifest) {
    const std::vector<std::uint8_t> empty;
    const auto manifest =
        linko::makeManifest("empty.txt", 0, linko::sha256Hex(empty));
    EXPECT_EQ(manifest.chunkCount, 0U);

    // The manifest alone completes an empty file: no chunk exists to send.
    linko::Reassembler reassembler(manifest);
    EXPECT_EQ(reassembler.addChunk(0, {}), linko::AddResult::Unexpected);
    EXPECT_EQ(reassembler.nextIndexNeeded(), manifest.chunkCount);

    const auto assembled = reassembler.takeIfCompleteAndValid();
    ASSERT_TRUE(assembled.has_value());
    EXPECT_TRUE(
        assembled->empty()); // NOLINT(bugprone-unchecked-optional-access)
}
