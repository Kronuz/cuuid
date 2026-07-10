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

// cuuid v6: the same condensed-UUID codec as v1 (cuuid_common), with two differences:
//   1. the compacted node is reconstructed with splitmix64 instead of std::mt19937, so a
//      read never pays a Mersenne Twister construction; and
//   2. the 16-byte value is laid out in RFC-9562 UUIDv6 order (version 6, timestamp first),
//      so the canonical form is byte-sortable by time.
// The wire framing, bit layout, salt derivation, and time base are all shared with v1.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cuuid_v1.hh"     // for the shared UUID container and UUIDGenerator (platform mint)


namespace cuuid_v6 {

// Compose a v6 UUID (UUIDv6 canonical layout, version 6, RFC-4122 variant) from raw
// (time, clock, node) fields WITHOUT compacting. `time` is the 60-bit Gregorian-100ns
// timestamp, as UUID::uuid1_time() returns.
UUID from_fields(uint64_t time, uint16_t clock, uint64_t node);

// Same, but compacted: the node is reconstructed with splitmix64 so serialise() yields the
// minimal compact wire.
UUID make(uint64_t time, uint16_t clock, uint64_t node);

// Mint a fresh, compacted v6 UUID from the platform clock.
UUID generate();

// Condensed v6 wire for a v6 UUID (shared condenser + splitmix64 expander).
std::string serialise(const UUID& uuid);

// Decode a condensed v6 wire into a v6 UUID; advances *ptr past the consumed bytes.
UUID unserialise(const char** ptr, const char* end);
UUID unserialise(std::string_view bytes);

} // namespace cuuid_v6
