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

#include "cuuid_v6.hh"

#include <array>

#include "cuuid_common.h"                          // for condense/uncondense/crush_fields, splitmix64


namespace {

// v6's node expander: splitmix64 in place of v1's std::mt19937. Same fnv seed and
// salt/multicast assembly (cuuid_common::calculate_node); this is the whole speed win.
uint64_t expand_splitmix(uint32_t seed) {
	return splitmix64(seed);
}


// (time, clock, node) -> RFC-9562 UUIDv6 canonical bytes (version 6, RFC-4122 variant).
// The 60-bit Gregorian timestamp is split time-high-first so the value sorts by time.
std::array<unsigned char, 16> to_v6_bytes(uint64_t time, uint16_t clock, uint64_t node) {
	std::array<unsigned char, 16> b{};
	uint32_t time_high = static_cast<uint32_t>((time >> 28) & 0xffffffff);
	uint16_t time_mid  = static_cast<uint16_t>((time >> 12) & 0xffff);
	uint16_t time_low  = static_cast<uint16_t>(time & 0x0fff);
	b[0] = static_cast<unsigned char>(time_high >> 24);
	b[1] = static_cast<unsigned char>(time_high >> 16);
	b[2] = static_cast<unsigned char>(time_high >> 8);
	b[3] = static_cast<unsigned char>(time_high);
	b[4] = static_cast<unsigned char>(time_mid >> 8);
	b[5] = static_cast<unsigned char>(time_mid);
	b[6] = static_cast<unsigned char>(0x60 | (time_low >> 8)); // version 6
	b[7] = static_cast<unsigned char>(time_low);
	b[8] = static_cast<unsigned char>(0x80 | ((clock >> 8) & 0x3f)); // RFC-4122 variant + clock hi
	b[9] = static_cast<unsigned char>(clock);
	for (int i = 0; i < 6; ++i) {
		b[10 + i] = static_cast<unsigned char>(node >> ((5 - i) * 8));
	}
	return b;
}


// Inverse of to_v6_bytes.
void from_v6_bytes(const std::array<unsigned char, 16>& b, uint64_t& time, uint16_t& clock, uint64_t& node) {
	uint64_t time_high = (static_cast<uint64_t>(b[0]) << 24) | (static_cast<uint64_t>(b[1]) << 16) |
	                     (static_cast<uint64_t>(b[2]) << 8) | b[3];
	uint64_t time_mid  = (static_cast<uint64_t>(b[4]) << 8) | b[5];
	uint64_t time_low  = (static_cast<uint64_t>(b[6] & 0x0f) << 8) | b[7];
	time  = (time_high << 28) | (time_mid << 12) | time_low;
	clock = static_cast<uint16_t>(((b[8] & 0x3f) << 8) | b[9]);
	node  = 0;
	for (int i = 0; i < 6; ++i) {
		node = (node << 8) | b[10 + i];
	}
}

} // namespace


namespace cuuid_v6 {

UUID
from_fields(uint64_t time, uint16_t clock, uint64_t node)
{
	return UUID(to_v6_bytes(time, clock, node));
}


UUID
make(uint64_t time, uint16_t clock, uint64_t node)
{
	crush_fields(time, clock, node, expand_splitmix);
	return UUID(to_v6_bytes(time, clock, node));
}


UUID
generate()
{
	UUIDGenerator gen;
	UUID base = gen(false); // a fresh, uncompacted, version-1 time UUID
	return make(base.uuid1_time(), base.uuid1_clock_seq(), base.uuid1_node());
}


std::string
serialise(const UUID& uuid)
{
	uint64_t time, node;
	uint16_t clock;
	from_v6_bytes(uuid.get_bytes(), time, clock, node);
	return condense(time, clock, node, expand_splitmix);
}


UUID
unserialise(const char** ptr, const char* end)
{
	uint64_t time, node;
	uint16_t clock;
	uncondense(ptr, end, time, clock, node, expand_splitmix);
	return UUID(to_v6_bytes(time, clock, node));
}


UUID
unserialise(std::string_view bytes)
{
	const char* pos = bytes.data();
	const char* end = pos + bytes.size();
	return unserialise(&pos, end);
}

} // namespace cuuid_v6
