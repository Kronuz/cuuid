/*
 * Copyright (c) 2015-2018 Dubalu LLC
 * Copyright (c) 2014 Graeme Hill (http://graemehill.ca).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Format-agnostic pieces shared by the cuuid v1 and v6 codecs: the condensed-UUID
// bit layout (UUIDCondenser), its variable-length wire serialise/unserialise, and the
// small primitives (pack/unpack, fnv_1a, xor_fold, splitmix64). The only per-version
// knob is the node expander passed to calculate_node(): v1 supplies a std::mt19937
// expander, v6 supplies splitmix64. Everything else is identical between the formats.

#pragma once

#include <cstddef>         // for size_t
#include <cstdint>         // for uint8_t, uint16_t, uint32_t, uint64_t
#include <string>          // for std::string

#ifdef CUUID_EXCEPTION_HEADER
#include CUUID_EXCEPTION_HEADER
#else
#include "cuuid_exception.h"
#endif
#ifdef CUUID_TRACE_HEADER
#include CUUID_TRACE_HEADER
#else
#include "cuuid_trace.h"
#endif


// 0x01b21dd213814000 is the number of 100-ns intervals between the
// UUID epoch 1582-10-15 00:00:00 and the Unix epoch 1970-01-01 00:00:00.
// 0x00011f0241243c00 = 1yr (365.2425 x 24 x 60 x 60 = 31556952s = 31556952000000000 nanoseconds)
constexpr uint64_t UUID_TIME_EPOCH             = 0x01b21dd213814000ULL;
constexpr uint64_t UUID_TIME_YEAR              = 0x00011f0241243c00ULL;
constexpr uint64_t UUID_TIME_INITIAL           = UUID_TIME_EPOCH + (2016 - 1970) * UUID_TIME_YEAR;
constexpr uint8_t  UUID_MAX_SERIALISED_LENGTH  = 17;

constexpr uint8_t TIME_BITS       = 60;
constexpr uint8_t PADDING_C0_BITS = 64 - TIME_BITS;
constexpr uint8_t PADDING_E0_BITS = 64 - TIME_BITS;
constexpr uint8_t COMPACTED_BITS  = 1;
constexpr uint8_t SALT_BITS       = 7;
constexpr uint8_t CLOCK_BITS      = 14;
constexpr uint8_t NODE_BITS       = 48;
constexpr uint8_t PADDING_C1_BITS = 64 - COMPACTED_BITS - SALT_BITS - CLOCK_BITS;
constexpr uint8_t PADDING_E1_BITS = 64 - COMPACTED_BITS - NODE_BITS - CLOCK_BITS;

constexpr uint64_t TIME_MASK     =  ((1ULL << TIME_BITS)    - 1);
constexpr uint64_t SALT_MASK     =  ((1ULL << SALT_BITS)    - 1);
constexpr uint64_t CLOCK_MASK    =  ((1ULL << CLOCK_BITS)   - 1);
constexpr uint64_t NODE_MASK     =  ((1ULL << NODE_BITS)    - 1);

// Multicast bit: a condensed node always carries it, so a reconstructed node is
// distinguishable from a real (globally administered) MAC address.
constexpr uint64_t MULTICAST_BIT = 0x010000000000ULL;

// Variable-length length encoding table for condensed UUIDs (prefix, mask)
inline constexpr uint8_t VL[13][2][2] = {
	{ { 0x1c, 0xfc }, { 0x1c, 0xfc } },  // 4:  00011100 11111100  00011100 11111100
	{ { 0x18, 0xfc }, { 0x18, 0xfc } },  // 5:  00011000 11111100  00011000 11111100
	{ { 0x14, 0xfc }, { 0x14, 0xfc } },  // 6:  00010100 11111100  00010100 11111100
	{ { 0x10, 0xfc }, { 0x10, 0xfc } },  // 7:  00010000 11111100  00010000 11111100
	{ { 0x04, 0xfc }, { 0x40, 0xc0 } },  // 8:  00000100 11111100  01000000 11000000
	{ { 0x0a, 0xfe }, { 0xa0, 0xe0 } },  // 9:  00001010 11111110  10100000 11100000
	{ { 0x08, 0xfe }, { 0x80, 0xe0 } },  // 10: 00001000 11111110  10000000 11100000
	{ { 0x02, 0xff }, { 0x20, 0xf0 } },  // 11: 00000010 11111111  00100000 11110000
	{ { 0x03, 0xff }, { 0x30, 0xf0 } },  // 12: 00000011 11111111  00110000 11110000
	{ { 0x0c, 0xff }, { 0xc0, 0xf0 } },  // 13: 00001100 11111111  11000000 11110000
	{ { 0x0d, 0xff }, { 0xd0, 0xf0 } },  // 14: 00001101 11111111  11010000 11110000
	{ { 0x0e, 0xff }, { 0xe0, 0xf0 } },  // 15: 00001110 11111111  11100000 11110000
	{ { 0x0f, 0xff }, { 0xf0, 0xf0 } },  // 16: 00001111 11111111  11110000 11110000
};


template <typename T>
inline void
pack(char** p, T num)
{
	auto end = *p + sizeof(num);
	auto ptr = end;
	for (size_t i = 0; i < sizeof(num); ++i) {
		*--ptr = static_cast<char>(num & 0xff);
		num >>= 8;
	}
	*p = end;
}


template <typename T>
inline T
unpack(char** const p)
{
	T num = 0;
	auto ptr = *p;
	for (size_t i = 0; i < sizeof(num); ++i) {
		num <<= 8;
		num |= static_cast<unsigned char>(*ptr++);
	}
	*p = ptr;
	return num;
}


inline uint64_t fnv_1a(uint64_t num) {
	// calculate FNV-1a hash
	uint64_t fnv = 0xcbf29ce484222325ULL;
	while (num != 0u) {
		fnv ^= num & 0xff;
		fnv *= 0x100000001b3ULL;
		num >>= 8;
	}
	return fnv;
}


inline uint64_t xor_fold(uint64_t num, int bits) {
	// xor-fold to n bits:
	uint64_t folded = 0;
	while (num != 0u) {
		folded ^= num;
		num >>= bits;
	}
	return folded;
}


// splitmix64: a cheap, deterministic 64-bit mixer. v6 uses it to reconstruct a
// compacted node's bits without the per-call std::mt19937 construction v1 pays.
inline uint64_t splitmix64(uint64_t x) {
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}


/*
 * Union for condensed UUIDs
 */
union UUIDCondenser {
	struct value_t {
		uint64_t val0;
		uint64_t val1;
	} value;

	struct compact_t {
		uint64_t time        : TIME_BITS;
		uint64_t padding0    : PADDING_C0_BITS;

		uint64_t compacted   : COMPACTED_BITS;
		uint64_t padding1    : PADDING_C1_BITS;
		uint64_t salt        : SALT_BITS;
		uint64_t clock       : CLOCK_BITS;
	} compact;

	struct expanded_t {
		uint64_t time        : TIME_BITS;
		uint64_t padding0    : PADDING_E0_BITS;

		uint64_t compacted   : COMPACTED_BITS;
		uint64_t padding1    : PADDING_E1_BITS;
		uint64_t node        : NODE_BITS;
		uint64_t clock       : CLOCK_BITS;
	} expanded;

	std::string serialise() const;
	static UUIDCondenser unserialise(const char** ptr, const char* end);

	UUIDCondenser();
};


// A node expander turns the 32-bit fnv seed into the 64-bit raw node bits. This is the
// only place v1 and v6 differ in node reconstruction: v1 passes a std::mt19937 expander,
// v6 passes splitmix64. See calculate_node().
using NodeExpander = uint64_t (*)(uint32_t seed);


// Reconstruct a compacted node from the condenser's (time, clock, salt), using the given
// expander for the seed -> raw-node step. Identical for v1 and v6 apart from the expander.
uint64_t calculate_node(const UUIDCondenser& condenser, NodeExpander expand);


// The (time, clock, node) <-> condensed-wire core, shared by v1 and v6. `time` is the 60-bit
// Gregorian-100ns UUID timestamp, `clock` the 14-bit clock sequence, `node` the 48-bit node.
// The only per-format input is `expand` (v1: mt19937, v6: splitmix64); the 16-byte canonical
// layout (v1 order vs UUIDv6 order) is the caller's job, done through its own field accessors.

// Crush (time, clock, node) in place to compacted form (salt derivation + node reconstruction).
void crush_fields(uint64_t& time, uint16_t& clock, uint64_t& node, NodeExpander expand);

// (time, clock, node) -> condensed wire.
std::string condense(uint64_t time, uint16_t clock, uint64_t node, NodeExpander expand);

// condensed wire -> (time, clock, node); advances *ptr past the consumed bytes.
void uncondense(const char** ptr, const char* end, uint64_t& time, uint16_t& clock, uint64_t& node, NodeExpander expand);

