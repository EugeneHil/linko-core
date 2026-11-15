// SHA-256 (FIPS 180-4); see include/linko/sha256.hpp for the contract.
// Self-contained vendored implementation following the reference structure of
// Brad Conte's public-domain sha256.c, rewritten for this codebase's style.
// The known-answer tests in tests/test_file_transfer.cpp pin the digests of
// the empty string and "abc" from FIPS 180-4.
#include "linko/sha256.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace linko {
namespace {

// Sizes: 64-byte compression blocks, 32-bit words, big-endian on the wire.
constexpr std::size_t kBlockSize = 64;
constexpr unsigned kWordBits = 32;
constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kWordBytes = kWordBits / kBitsPerByte;
// Words of the message schedule sourced from each block; the remaining 48
// are derived.
constexpr std::size_t kWordsPerBlock = 16;
// The trailing big-endian bit-length field of the padding.
constexpr std::size_t kLengthFieldBytes = 8;
// First padding byte and the mask that extracts one byte of a word.
constexpr std::uint8_t kPaddingStart = 0x80;
constexpr std::uint64_t kByteMask = 0xFF;
constexpr std::uint8_t kLowNibbleMask = 0x0F;
constexpr std::size_t kHexDigitsPerByte = 2;

// Round constants (FIPS 180-4 section 4.2.2): the first 32 bits of the
// fractional parts of the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// Initial hash values (FIPS 180-4 section 5.3.3): the first 32 bits of the
// fractional parts of the square roots of the first 8 primes.
constexpr std::array<std::uint32_t, 8> kInitialState{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// Message-schedule reach-backs (FIPS 180-4 section 6.2.2).
constexpr std::size_t kBack16 = 16;
constexpr std::size_t kBack15 = 15;
constexpr std::size_t kBack7 = 7;
constexpr std::size_t kBack2 = 2;

// Rotations (FIPS 180-4 section 4.1.2): Sigma0/Sigma1 in the compression
// step are pure rotation triples; sigma0/sigma1 in the message schedule mix
// two rotations with one plain (unrotated) shift.
constexpr std::array<unsigned, 3> kBigSigma0Rotations{2, 13, 22};
constexpr std::array<unsigned, 3> kBigSigma1Rotations{6, 11, 25};
constexpr std::array<unsigned, 2> kSigma0Rotations{7, 18};
constexpr std::array<unsigned, 2> kSigma1Rotations{17, 19};
constexpr unsigned kSigma0Shift = 3;
constexpr unsigned kSigma1Shift = 10;

// Working-variable slots in the compression step (FIPS 180-4 names a..h).
constexpr std::size_t kRegA = 0;
constexpr std::size_t kRegB = 1;
constexpr std::size_t kRegC = 2;
constexpr std::size_t kRegD = 3;
constexpr std::size_t kRegE = 4;
constexpr std::size_t kRegF = 5;
constexpr std::size_t kRegG = 6;
constexpr std::size_t kRegH = 7;

auto rotr(std::uint32_t value, unsigned bits) -> std::uint32_t {
    return (value >> bits) | (value << (kWordBits - bits));
}

// ROTR(x, r0) ^ ROTR(x, r1) ^ ROTR(x, r2) for a rotation triple.
auto xorRotations(std::uint32_t value, const std::array<unsigned, 3> &rotations)
    -> std::uint32_t {
    return rotr(value, rotations[0]) ^ rotr(value, rotations[1]) ^
           rotr(value, rotations[2]);
}

// A big-endian 32-bit word read from `bytes`.
auto readWordBE(const std::uint8_t *bytes) -> std::uint32_t {
    std::uint32_t word = 0;
    for (unsigned byte = 0; byte < kWordBytes; ++byte) {
        word = (word << kBitsPerByte) | static_cast<std::uint32_t>(bytes[byte]);
    }
    return word;
}

// One compression step: folds `block` (kBlockSize bytes) into `state`
// (FIPS 180-4 section 6.2.2).
auto compress(std::array<std::uint32_t, kInitialState.size()> &state,
              const std::uint8_t *block) -> void {
    std::array<std::uint32_t, kRoundConstants.size()> schedule{};
    for (std::size_t i = 0; i < kWordsPerBlock; ++i) {
        schedule[i] = readWordBE(block + (i * kWordBytes));
    }
    for (std::size_t i = kWordsPerBlock; i < schedule.size(); ++i) {
        const std::uint32_t &reach15 = schedule[i - kBack15];
        const std::uint32_t &reach2 = schedule[i - kBack2];
        const std::uint32_t sigma0 = rotr(reach15, kSigma0Rotations[0]) ^
                                     rotr(reach15, kSigma0Rotations[1]) ^
                                     (reach15 >> kSigma0Shift);
        const std::uint32_t sigma1 = rotr(reach2, kSigma1Rotations[0]) ^
                                     rotr(reach2, kSigma1Rotations[1]) ^
                                     (reach2 >> kSigma1Shift);
        schedule[i] =
            schedule[i - kBack16] + sigma0 + schedule[i - kBack7] + sigma1;
    }

    std::array<std::uint32_t, kInitialState.size()> work = state;
    for (std::size_t i = 0; i < kRoundConstants.size(); ++i) {
        const std::uint32_t bigSigma1 =
            xorRotations(work[kRegE], kBigSigma1Rotations);
        const std::uint32_t choose =
            (work[kRegE] & work[kRegF]) ^ (~work[kRegE] & work[kRegG]);
        const std::uint32_t temp1 =
            work[kRegH] + bigSigma1 + choose + kRoundConstants[i] + schedule[i];
        const std::uint32_t bigSigma0 =
            xorRotations(work[kRegA], kBigSigma0Rotations);
        const std::uint32_t majority = (work[kRegA] & work[kRegB]) ^
                                       (work[kRegA] & work[kRegC]) ^
                                       (work[kRegB] & work[kRegC]);
        const std::uint32_t temp2 = bigSigma0 + majority;
        // The FIPS 180-4 shift chain: h=g, g=f, f=e, e=d+t1, d=c, c=b, b=a,
        // a=t1+t2.
        work[kRegH] = work[kRegG];
        work[kRegG] = work[kRegF];
        work[kRegF] = work[kRegE];
        work[kRegE] = work[kRegD] + temp1;
        work[kRegD] = work[kRegC];
        work[kRegC] = work[kRegB];
        work[kRegB] = work[kRegA];
        work[kRegA] = temp1 + temp2;
    }
    for (std::size_t i = 0; i < state.size(); ++i) {
        state[i] += work[i];
    }
}

} // namespace

auto sha256(const std::vector<std::uint8_t> &data) -> Sha256Digest {
    std::array<std::uint32_t, kInitialState.size()> state = kInitialState;

    const std::size_t fullBlocks = data.size() / kBlockSize;
    for (std::size_t block = 0; block < fullBlocks; ++block) {
        compress(state, data.data() + (block * kBlockSize));
    }

    // The tail (fewer than kBlockSize bytes) plus padding: a 0x80 byte,
    // zeros, and the 8-byte big-endian bit length. When the tail is 56 bytes
    // or more the length no longer fits in the same block, so the padding
    // spills into a second one.
    const std::size_t remainder = data.size() % kBlockSize;
    const std::size_t paddedLength =
        (remainder + 1 + kLengthFieldBytes > kBlockSize) ? 2 * kBlockSize
                                                         : kBlockSize;
    std::array<std::uint8_t, 2 * kBlockSize> tail{};
    std::copy(data.end() - static_cast<std::ptrdiff_t>(remainder), data.end(),
              tail.begin());
    tail[remainder] = kPaddingStart;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) *
                                    static_cast<std::uint64_t>(kBitsPerByte);
    for (std::size_t i = 0; i < kLengthFieldBytes; ++i) {
        const auto shift =
            static_cast<unsigned>(kBitsPerByte * (kLengthFieldBytes - 1U - i));
        tail[(paddedLength - kLengthFieldBytes) + i] =
            static_cast<std::uint8_t>((bitLength >> shift) & kByteMask);
    }

    compress(state, tail.data());
    if (paddedLength == 2 * kBlockSize) {
        compress(state, tail.data() + kBlockSize);
    }

    Sha256Digest digest{};
    for (std::size_t word = 0; word < state.size(); ++word) {
        for (unsigned byte = 0; byte < kWordBytes; ++byte) {
            const auto shift = (kWordBytes - 1U - byte) * kBitsPerByte;
            digest[(word * kWordBytes) + byte] =
                static_cast<std::uint8_t>(state[word] >> shift);
        }
    }
    return digest;
}

auto toHex(const Sha256Digest &digest) -> std::string {
    static constexpr std::string_view kHexDigits{"0123456789abcdef"};
    constexpr unsigned kHighNibbleShift = 4;
    std::string hex;
    hex.reserve(digest.size() * kHexDigitsPerByte);
    for (const std::uint8_t byte : digest) {
        hex.push_back(
            kHexDigits[static_cast<std::size_t>(byte >> kHighNibbleShift)]);
        hex.push_back(
            kHexDigits[static_cast<std::size_t>(byte & kLowNibbleMask)]);
    }
    return hex;
}

auto sha256Hex(const std::vector<std::uint8_t> &data) -> std::string {
    return toHex(sha256(data));
}

} // namespace linko
