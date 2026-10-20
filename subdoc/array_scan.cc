/*
 *     Copyright 2026-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

/*
 * The array is scanned in blocks of 64 bytes. Each block is classified into
 * bitmasks (one bit per byte) of the characters of interest, the bits for
 * characters inside strings are masked out (by tracking unescaped quotes
 * with a prefix xor), and only the bits for brackets and top-level commas
 * are then visited. This lets us skip over the contents of the array
 * without looking at each character individually.
 */

#include "array_scan.h"
#include "util.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define SUBDOC_ARRAY_SCAN_NEON 1
#elif defined(__AVX2__)
#include <immintrin.h>
#define SUBDOC_ARRAY_SCAN_AVX2 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define SUBDOC_ARRAY_SCAN_SSE2 1
#endif

/**
 * Force inlining of classify(). It is called once per 64 byte block in the
 * inner loop of the scanner, and the compiler doesn't always inline it (GCC
 * at -O2 emits a call returning the masks through memory, and with AVX2 a
 * vzeroupper on every call).
 */
#if defined(_MSC_VER)
#define SUBDOC_ALWAYS_INLINE __forceinline
#else
#define SUBDOC_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

namespace Subdoc {

namespace {

constexpr size_t BlockSize = 64;

/** Bitmasks for the characters of interest in a 64 byte block */
struct BlockMasks {
    uint64_t quote;
    uint64_t backslash;
    /** '[' and '{' */
    uint64_t open;
    /** ']' and '}' */
    uint64_t close;
    uint64_t comma;
};

#if defined(SUBDOC_ARRAY_SCAN_NEON)
/**
 * Build a 64 bit mask from 4 vectors where each lane is either 0xff or 0
 * (NEON lacks an equivalent to SSE's movemask)
 */
inline uint64_t movemask(uint8x16_t v0,
                         uint8x16_t v1,
                         uint8x16_t v2,
                         uint8x16_t v3) {
    const uint8x16_t bits = {
            1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    uint8x16_t sum0 = vpaddq_u8(vandq_u8(v0, bits), vandq_u8(v1, bits));
    uint8x16_t sum1 = vpaddq_u8(vandq_u8(v2, bits), vandq_u8(v3, bits));
    sum0 = vpaddq_u8(sum0, sum1);
    sum0 = vpaddq_u8(sum0, sum0);
    return vgetq_lane_u64(vreinterpretq_u64_u8(sum0), 0);
}

SUBDOC_ALWAYS_INLINE BlockMasks classify(const char* block) {
    const auto* ptr = reinterpret_cast<const uint8_t*>(block);
    std::array<uint8x16_t, 4> chunks{{vld1q_u8(ptr),
                                      vld1q_u8(ptr + 16),
                                      vld1q_u8(ptr + 32),
                                      vld1q_u8(ptr + 48)}};
    // '[' (0x5b) and '{' (0x7b) only differ in bit 0x20, and so do
    // ']' (0x5d) and '}' (0x7d)
    const uint8x16_t case_bit = vdupq_n_u8(0x20);
    auto match = [&chunks](auto transform, uint8_t ch) {
        const uint8x16_t needle = vdupq_n_u8(ch);
        return movemask(vceqq_u8(transform(chunks[0]), needle),
                        vceqq_u8(transform(chunks[1]), needle),
                        vceqq_u8(transform(chunks[2]), needle),
                        vceqq_u8(transform(chunks[3]), needle));
    };
    auto identity = [](uint8x16_t v) { return v; };
    auto fold = [case_bit](uint8x16_t v) { return vorrq_u8(v, case_bit); };
    return {.quote = match(identity, '"'),
            .backslash = match(identity, '\\'),
            .open = match(fold, '{'),
            .close = match(fold, '}'),
            .comma = match(identity, ',')};
}
#elif defined(SUBDOC_ARRAY_SCAN_AVX2)
SUBDOC_ALWAYS_INLINE BlockMasks classify(const char* block) {
    const __m256i chunk0 =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(block));
    const __m256i chunk1 =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(block + 32));
    // '[' (0x5b) and '{' (0x7b) only differ in bit 0x20, and so do
    // ']' (0x5d) and '}' (0x7d)
    const __m256i case_bit = _mm256_set1_epi8(0x20);
    auto match = [chunk0, chunk1](auto transform, char ch) {
        const __m256i needle = _mm256_set1_epi8(ch);
        const auto mask0 = static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(transform(chunk0), needle)));
        const auto mask1 = static_cast<uint32_t>(_mm256_movemask_epi8(
                _mm256_cmpeq_epi8(transform(chunk1), needle)));
        return uint64_t(mask0) | (uint64_t(mask1) << 32);
    };
    auto identity = [](__m256i v) { return v; };
    auto fold = [case_bit](__m256i v) { return _mm256_or_si256(v, case_bit); };
    return {.quote = match(identity, '"'),
            .backslash = match(identity, '\\'),
            .open = match(fold, '{'),
            .close = match(fold, '}'),
            .comma = match(identity, ',')};
}
#elif defined(SUBDOC_ARRAY_SCAN_SSE2)
SUBDOC_ALWAYS_INLINE BlockMasks classify(const char* block) {
    // Separate variables rather than a std::array, as GCC ignores the
    // attributes of __m128i used as a template argument (and warns)
    const __m128i chunk0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(block));
    const __m128i chunk1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 16));
    const __m128i chunk2 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 32));
    const __m128i chunk3 =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 48));
    // '[' (0x5b) and '{' (0x7b) only differ in bit 0x20, and so do
    // ']' (0x5d) and '}' (0x7d)
    const __m128i case_bit = _mm_set1_epi8(0x20);
    auto match = [chunk0, chunk1, chunk2, chunk3](auto transform, char ch) {
        const __m128i needle = _mm_set1_epi8(ch);
        auto bits = [&transform, needle](__m128i chunk) {
            return uint64_t(static_cast<uint32_t>(_mm_movemask_epi8(
                    _mm_cmpeq_epi8(transform(chunk), needle))));
        };
        return bits(chunk0) | (bits(chunk1) << 16) | (bits(chunk2) << 32) |
               (bits(chunk3) << 48);
    };
    auto identity = [](__m128i v) { return v; };
    auto fold = [case_bit](__m128i v) { return _mm_or_si128(v, case_bit); };
    return {.quote = match(identity, '"'),
            .backslash = match(identity, '\\'),
            .open = match(fold, '{'),
            .close = match(fold, '}'),
            .comma = match(identity, ',')};
}
#else
SUBDOC_ALWAYS_INLINE BlockMasks classify(const char* block) {
    BlockMasks masks{};
    for (size_t ii = 0; ii < BlockSize; ++ii) {
        const uint64_t bit = uint64_t(1) << ii;
        switch (block[ii]) {
        case '"':
            masks.quote |= bit;
            break;
        case '\\':
            masks.backslash |= bit;
            break;
        case '[':
        case '{':
            masks.open |= bit;
            break;
        case ']':
        case '}':
            masks.close |= bit;
            break;
        case ',':
            masks.comma |= bit;
            break;
        default:
            break;
        }
    }
    return masks;
}
#endif

/**
 * Compute the mask of characters which are escaped by a preceding
 * backslash.
 *
 * @param backslash The mask of backslashes in the block
 * @param prev_escaped [in/out] Set if the first character in the block is
 *                     escaped by the last character in the previous block,
 *                     and updated to tell if the first character in the
 *                     next block is escaped.
 */
uint64_t find_escaped(uint64_t backslash, bool& prev_escaped) {
    uint64_t escaped = 0;
    if (prev_escaped) {
        // The first character is escaped (and if it is a backslash it
        // can't escape the next character)
        escaped = 1;
        backslash &= ~uint64_t(1);
        prev_escaped = false;
    }
    // Backslashes are rare in most documents so just walk them one by one
    while (backslash) {
        const auto idx = std::countr_zero(backslash);
        if (idx == BlockSize - 1) {
            prev_escaped = true;
            break;
        }
        escaped |= uint64_t(1) << (idx + 1);
        // The next character is escaped and can't start an escape sequence
        backslash &= ~(uint64_t(3) << idx);
    }
    return escaped;
}

/**
 * Compute a mask where bit N is set if there is an odd number of bits set
 * in the input from bit 0 to N (inclusive). Applied to the mask of
 * (unescaped) quotes this gives the mask of the characters inside strings.
 */
uint64_t prefix_xor(uint64_t bits) {
    bits ^= bits << 1;
    bits ^= bits << 2;
    bits ^= bits << 4;
    bits ^= bits << 8;
    bits ^= bits << 16;
    bits ^= bits << 32;
    return bits;
}

/** Mask with the bits in the range [lo, hi) set (0 <= lo <= hi <= 64) */
uint64_t range_mask(size_t lo, size_t hi) {
    const uint64_t below_hi =
            hi >= BlockSize ? ~uint64_t(0) : (uint64_t(1) << hi) - 1;
    const uint64_t below_lo =
            lo >= BlockSize ? ~uint64_t(0) : (uint64_t(1) << lo) - 1;
    return below_hi & ~below_lo;
}

/**
 * Locate the first unescaped quote at or after the block at the given
 * offset. Only the quotes and backslashes are looked at, which makes this
 * faster than processing the blocks in scan_container_impl() when skipping
 * the contents of a (long) string.
 *
 * @param begin The beginning of the buffer
 * @param size The size of the buffer
 * @param offset The offset of the first block to look at
 * @param prev_escaped Set if the first character in that block is escaped
 * @return The offset of the quote, or std::string_view::npos if there is
 *         no unescaped quote in the rest of the buffer
 */
size_t find_unescaped_quote(const char* begin,
                            size_t size,
                            size_t offset,
                            bool prev_escaped) {
    std::array<char, BlockSize> tail;
    for (; offset < size; offset += BlockSize) {
        const char* block = begin + offset;
        if (size - offset < BlockSize) {
            // Pad the last block with whitespace
            tail.fill(' ');
            std::memcpy(tail.data(), block, size - offset);
            block = tail.data();
        }
        const auto masks = classify(block);
        uint64_t quotes = masks.quote;
        if (masks.backslash || prev_escaped) {
            quotes &= ~find_escaped(masks.backslash, prev_escaped);
        }
        if (quotes) {
            return offset + std::countr_zero(quotes);
        }
    }
    return std::string_view::npos;
}

/** Value for scan_container_impl()'s target when not locating an element */
constexpr size_t NoTarget = std::numeric_limits<size_t>::max();

/**
 * Check if the container whose opening bracket (or brace) is at the given
 * offset is empty
 */
bool is_empty_container(const char* begin, size_t size, size_t pos) {
    size_t idx = pos + 1;
    while (idx < size && is_json_ws(begin[idx])) {
        ++idx;
    }
    return idx < size && (begin[idx] == ']' || begin[idx] == '}');
}

/** Check if the buffer starts with an array (rather than an object) */
bool is_array(std::string_view buffer) {
    return !buffer.empty() && buffer.front() == '[';
}

/**
 * Scan the array (or object) at the beginning of the buffer.
 *
 * @param buffer The buffer to scan (starting with the opening bracket or
 *               brace)
 * @param visitor If non-null, called with each top-level element (with
 *                surrounding whitespace removed) in order, and the scan
 *                stops with Status::Found when it returns false. When
 *                visiting elements the scan stops with Status::NonPrimitive
 *                if an element is an array or object (once the element has
 *                been scanned). Must be null when scanning an object.
 * @param target If not NoTarget (and visitor is null), the index of the
 *               element to locate; the scan stops with Status::Found once
 *               the element is located.
 * @param max_depth The maximum nesting depth (see scan_array())
 */
template <typename Visitor>
ArrayScanResult scan_container_impl(
        std::string_view buffer,
        Visitor* visitor,
        size_t target = NoTarget,
        size_t max_depth = std::numeric_limits<size_t>::max()) {
    using Status = ArrayScanResult::Status;
    ArrayScanResult result;
    if (buffer.empty() || (buffer.front() != '[' && buffer.front() != '{')) {
        return result;
    }
    if (max_depth == 0) {
        // Not even the container itself fits
        result.status = Status::TooDeep;
        return result;
    }
    const char close = buffer.front() == '[' ? ']' : '}';

    const char* const begin = buffer.data();
    const size_t size = buffer.size();

    // Check for an empty container up front so that we know that every
    // top-level comma separates two elements (or members)
    {
        size_t idx = 1;
        while (idx < size && is_json_ws(begin[idx])) {
            ++idx;
        }
        if (idx == size) {
            return result;
        }
        if (begin[idx] == close) {
            result.status = Status::Success;
            result.length = idx + 1;
            return result;
        }
    }

    // The depth includes the container itself (which is the first open
    // bracket we'll find), so top-level commas are at depth 1
    size_t depth = 0;
    size_t num_commas = 0;
    // Offset of the first character of the current top-level element
    // (only maintained when visiting elements)
    size_t element_begin = 1;
    // Set when the current top-level element is an array or object (only
    // when visiting elements)
    bool non_primitive = false;
    // Offset of the last top-level comma seen (0 if none)
    size_t last_comma = 0;
    // Carried over between blocks
    bool prev_escaped = false;
    uint64_t in_string_carry = 0;

    // Visit the element ending at pos, return false if the scan should
    // stop (with the result updated)
    auto visit = [&](size_t pos) -> bool {
        if (non_primitive) {
            result.status = Status::NonPrimitive;
            return false;
        }
        if (!(*visitor)(trim_json_ws({begin + element_begin, begin + pos}))) {
            result.status = Status::Found;
            result.found_index = num_commas;
            return false;
        }
        element_begin = pos + 1;
        return true;
    };

    // The target element ends at pos, return false (the scan should stop)
    auto found_target = [&](size_t pos, bool has_next) -> bool {
        const auto element = trim_json_ws({begin + element_begin, begin + pos});
        result.status = Status::Found;
        result.element_offset = size_t(element.data() - begin);
        result.element_length = element.size();
        result.has_next = has_next;
        return false;
    };

    // Process the top-level commas in the mask, return false if the
    // scan should stop (with the result updated)
    auto top_level_commas = [&](uint64_t commas, size_t offset) -> bool {
        if (visitor == nullptr && target != NoTarget && commas &&
            num_commas + std::popcount(commas) >= target) {
            // The target element begins and/or ends in this range
            while (commas) {
                const size_t pos = offset + std::countr_zero(commas);
                commas &= commas - 1;
                if (num_commas == target) {
                    return found_target(pos, true);
                }
                ++num_commas;
                element_begin = pos + 1;
            }
            return true;
        }
        if (visitor == nullptr) {
            if (commas) {
                num_commas += std::popcount(commas);
                last_comma =
                        offset + (BlockSize - 1) - std::countl_zero(commas);
            }
            return true;
        }
        while (commas) {
            const size_t pos = offset + std::countr_zero(commas);
            commas &= commas - 1;
            if (!visit(pos)) {
                return false;
            }
            ++num_commas;
        }
        return true;
    };

    std::array<char, BlockSize> tail;
    size_t offset = 0;
    while (offset < size) {
        // The offset of the next block to process
        size_t next = offset + BlockSize;
        const char* block = begin + offset;
        if (size - offset < BlockSize) {
            // Pad the last block with whitespace
            tail.fill(' ');
            std::memcpy(tail.data(), block, size - offset);
            block = tail.data();
        }

        const auto masks = classify(block);
        uint64_t quotes = masks.quote;
        if (masks.backslash || prev_escaped) {
            quotes &= ~find_escaped(masks.backslash, prev_escaped);
        }
        if (quotes == 0 && in_string_carry) {
            // The whole block is inside a (long) string; continue at the
            // quote ending it (which isn't escaped)
            offset = find_unescaped_quote(begin, size, next, prev_escaped);
            if (offset == std::string_view::npos) {
                break;
            }
            prev_escaped = false;
            continue;
        }
        const uint64_t in_string = prefix_xor(quotes) ^ in_string_carry;
        in_string_carry = uint64_t(int64_t(in_string) >> 63);

        uint64_t brackets = (masks.open | masks.close) & ~in_string;
        const uint64_t commas = masks.comma & ~in_string;

        // Commas are only interesting at the top level, so walk the
        // brackets and process the commas between them when we're at the
        // top level
        size_t prev = 0;
        while (brackets) {
            // The lowest remaining bracket, and its index in the block
            const uint64_t bracket = brackets & (~brackets + 1);
            const size_t idx = std::countr_zero(brackets);
            brackets &= brackets - 1;
            if (depth == 1 &&
                !top_level_commas(commas & range_mask(prev, idx), offset)) {
                return result;
            }
            prev = idx + 1;

            if (masks.open & bracket) {
                if (depth == 1 && visitor != nullptr) {
                    // Elements can't be compared with non-primitives. This
                    // is reported when the element is visited (once it has
                    // been scanned), so that a depth error in it takes
                    // precedence.
                    non_primitive = true;
                }
                ++depth;
                if (depth == max_depth) [[unlikely]] {
                    // A container at the maximum depth must be empty
                    if (!is_empty_container(begin, size, offset + idx)) {
                        result.status = Status::TooDeep;
                        return result;
                    }
                }
                continue;
            }

            if (depth == 0) {
                // Close before the array started?
                return result;
            }
            if (--depth == 0) {
                const size_t pos = offset + idx;
                if (begin[pos] != close) {
                    return result;
                }
                if (visitor != nullptr && !visit(pos)) {
                    return result;
                }
                if (target != NoTarget && num_commas == target) {
                    found_target(pos, false);
                    return result;
                }
                result.status = Status::Success;
                result.length = pos + 1;
                result.num_elements = num_commas + 1;
                if (target == NoTarget) {
                    const auto last = trim_json_ws(
                            {begin + (last_comma ? last_comma + 1 : 1),
                             begin + pos});
                    result.element_offset = size_t(last.data() - begin);
                    result.element_length = last.size();
                }
                return result;
            }
        }
        if (depth == 1 &&
            !top_level_commas(commas & range_mask(prev, BlockSize), offset)) {
            return result;
        }
        offset = next;
    }

    // Ran off the end of the buffer
    return result;
}

/** Placeholder visitor type used when elements aren't visited */
using NoVisitor = bool (*)(std::string_view);

} // namespace

ArrayScanResult scan_array(std::string_view buffer,
                           std::string_view needle,
                           size_t max_depth) {
    if (!is_array(buffer)) {
        return {};
    }
    if (needle.empty()) {
        return scan_container_impl<NoVisitor>(
                buffer, nullptr, NoTarget, max_depth);
    }
    auto compare = [needle](std::string_view element) {
        return element != needle;
    };
    return scan_container_impl(buffer, &compare, NoTarget, max_depth);
}

ArrayScanResult find_array_element(std::string_view buffer,
                                   size_t index,
                                   size_t max_depth) {
    if (!is_array(buffer)) {
        return {};
    }
    return scan_container_impl<NoVisitor>(buffer, nullptr, index, max_depth);
}

ArrayScanResult scan_container(std::string_view buffer, size_t max_depth) {
    return scan_container_impl<NoVisitor>(buffer, nullptr, NoTarget, max_depth);
}

size_t find_string_end(std::string_view buffer) {
    if (buffer.empty() || buffer.front() != '"') {
        return std::string_view::npos;
    }
    const auto quote = find_unescaped_quote(
            buffer.data() + 1, buffer.size() - 1, 0, false);
    // The offset from the opening quote
    return quote == std::string_view::npos ? quote : quote + 1;
}

ArrayScanResult filter_array(std::string_view buffer,
                             std::string_view needle,
                             bool remove_all,
                             std::string& survivors,
                             size_t max_depth) {
    // Survivors are copied in runs of consecutive elements (including the
    // separators between them) rather than one by one
    const char* run_begin = nullptr;
    const char* run_end = nullptr;
    size_t num_removed = 0;
    auto flush = [&]() {
        if (run_begin != nullptr) {
            if (!survivors.empty()) {
                survivors.push_back(',');
            }
            survivors.append(run_begin, run_end);
            run_begin = nullptr;
        }
    };
    auto filter = [&](std::string_view element) {
        if ((remove_all || num_removed == 0) && element == needle) {
            ++num_removed;
            flush();
        } else {
            if (run_begin == nullptr) {
                run_begin = element.data();
            }
            run_end = element.data() + element.size();
        }
        return true;
    };
    if (!is_array(buffer)) {
        return {};
    }
    auto result = scan_container_impl(buffer, &filter, NoTarget, max_depth);
    if (result.status == ArrayScanResult::Status::Success) {
        flush();
        result.num_removed = num_removed;
    }
    return result;
}

} // namespace Subdoc
