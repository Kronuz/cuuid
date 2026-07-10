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

#include "cuuid_common.h"

#include <algorithm>                              // for std::copy_n, std::fill
#include <cassert>                                // for assert

#ifdef CUUID_NODE_HEADER
#include CUUID_NODE_HEADER
#else
#include "cuuid_node.h"                           // for cuuid::local_node_hash
#endif


UUIDCondenser::UUIDCondenser()
	: compact({ 0, 0, 0, 0, 0, 0 }) { }


uint64_t
calculate_node(const UUIDCondenser& condenser, NodeExpander expand)
{
	L_CALL("calculate_node()");

	if ((condenser.compact.time == 0u) && (condenser.compact.clock == 0u) && (condenser.compact.salt == 0u)) {
		return MULTICAST_BIT;
	}

	uint32_t seed = 0;
	seed ^= fnv_1a(condenser.compact.time);
	seed ^= fnv_1a(condenser.compact.clock);
	seed ^= fnv_1a(condenser.compact.salt);
	uint64_t node = expand(seed);
	node &= NODE_MASK & ~SALT_MASK;
	node |= condenser.compact.salt;
	node |= MULTICAST_BIT; // set multicast bit
	return node;
}


std::string
UUIDCondenser::serialise() const
{
	L_CALL("UUIDCondenser::serialise()");

	uint64_t buf0, buf1;
	if (compact.compacted != 0u) {
	//           .       .       .       .       .       .       .       .           .       .       .       .       .       .       .       .
	// v0:PPPPTTTTTTTTTTTTTTTTTTtttttttttttttttttttttttttttttttttttttttttt v1:KKKKKKKKKKKKKKSSSSSSSPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPC
	// b0:                                              TTTTTTTTTTTTTTTTTT b1:ttttttttttttttttttttttttttttttttttttttttttKKKKKKKKKKKKKKSSSSSSSC
		assert(compact.padding0 == 0);
		assert(compact.padding1 == 0);
		buf0 = (value.val0 >> PADDING_C1_BITS);
		buf1 = (value.val0 << (64 - PADDING_C1_BITS)) | (value.val1 >> PADDING_C1_BITS) | 1;
	} else {
	//           .       .       .       .       .       .       .       .           .       .       .       .       .       .       .       .
	// v0:PPPPTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTt v1:KKKKKKKKKKKKKKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNPC
	// b0:     TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT b1:tKKKKKKKKKKKKKKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNC
		assert(expanded.padding0 == 0);
		assert(expanded.padding1 == 0);
		buf0 = (value.val0 >> PADDING_E1_BITS);
		buf1 = (value.val0 << (64 - PADDING_E1_BITS)) | (value.val1 >> PADDING_E1_BITS);
	}

	char buf[UUID_MAX_SERIALISED_LENGTH];

	char* end = buf;
	*end++ = '\0';
	pack(&end, buf0);
	pack(&end, buf1);
	end -= 4; // serialized must be at least 4 bytes long.

	auto ptr = buf;
	while (ptr != end && (*++ptr == 0)) {}; // remove all leading zeros

	auto length = end - ptr;
	if ((*ptr & VL[length][0][1]) != 0) {
		if ((*ptr & VL[length][1][1]) != 0) {
			--ptr;
			++length;
			*ptr |= VL[length][0][0];
		} else {
			*ptr |= VL[length][1][0];
		}
	} else {
		*ptr |= VL[length][0][0];
	}

	return std::string(ptr, length + 4);
}


UUIDCondenser
UUIDCondenser::unserialise(const char** ptr, const char* end)
{
	L_CALL("UUIDCondenser::unserialise({})", repr(*ptr, end));

	auto size = end - *ptr;
	auto length = size + 1;
	auto l = **ptr;
	bool q = (l & 0xf0) != 0;
	int i = 0;
	for (; i < 13; ++i) {
		if (VL[i][q][0] == (l & VL[i][q][1])) {
			length = i + 4;
			break;
		}
	}
	if (size < length) {
		THROW(SerialisationError, "Bad condensed UUID");
	}

	char buf[UUID_MAX_SERIALISED_LENGTH];
	auto start = buf + UUID_MAX_SERIALISED_LENGTH - length;
	std::fill(buf, start, 0);
	std::copy_n(*ptr, length, start);

	*start &= ~VL[i][q][1];

	char* p = &buf[1];
	auto buf0 = unpack<uint64_t>(&p);
	auto buf1 = unpack<uint64_t>(&p);

	UUIDCondenser condenser;
	if ((buf1 & 1) != 0u) {  // compacted
	//           .       .       .       .       .       .       .       .           .       .       .       .       .       .       .       .
	// b0:                                                TTTTTTTTTTTTTTTT b1:ttttttttttttttttttttttttttttttttttttttttttttKKKKKKKKKKKKKKSSSSSC
	// v0:PPPPTTTTTTTTTTTTTTTTtttttttttttttttttttttttttttttttttttttttttttt v1:KKKKKKKKKKKKKKSSSSSPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPC
		condenser.value.val0 = (buf0 << PADDING_C1_BITS) | (buf1 >> (64 - PADDING_C1_BITS));
		condenser.value.val1 = (buf1 << PADDING_C1_BITS) | 1;
	} else {
	//           .       .       .       .       .       .       .       .           .       .       .       .       .       .       .       .
	// b0:     TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT b1:tKKKKKKKKKKKKKKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNC
	// v0:PPPPTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTt v1:KKKKKKKKKKKKKKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNPC
		condenser.value.val0 = (buf0 << PADDING_E1_BITS) | (buf1 >> (64 - PADDING_E1_BITS));
		condenser.value.val1 = (buf1 << PADDING_E1_BITS);
	}

	*ptr += length;
	return condenser;
}


void
crush_fields(uint64_t& time, uint16_t& clock, uint64_t& node, NodeExpander expand)
{
	L_CALL("crush_fields()");

	auto compacted_time = time != 0u ? ((time - UUID_TIME_INITIAL) & TIME_MASK) : time;
	auto compacted_time_clock = compacted_time & CLOCK_MASK;
	compacted_time >>= CLOCK_BITS;

	UUIDCondenser condenser;
	condenser.compact.compacted = 1u;
	condenser.compact.clock = clock ^ compacted_time_clock;
	condenser.compact.time = compacted_time;
	if ((node & MULTICAST_BIT) != 0u) {
		condenser.compact.salt = node & SALT_MASK;
	} else {
		auto lnh = ::cuuid::local_node_hash();
		auto salt = fnv_1a((lnh ? *lnh : 0) | node); // bitwise OR mixes the node identity into the salt. (Was `|| node` in Xapiand -- a logical OR that collapsed every non-zero node to a single salt; fixed here, which changes newly-compacted UUID salt bits vs older versions.)
		salt = xor_fold(salt, SALT_BITS);
		condenser.compact.salt = salt & SALT_MASK;
	}

	node = calculate_node(condenser, expand);
	clock = static_cast<uint16_t>(condenser.compact.clock);

	uint64_t t = condenser.compact.time;
	if (t != 0u) {
		t = ((t << CLOCK_BITS) + UUID_TIME_INITIAL) & TIME_MASK;
	}
	time = t;
}


std::string
condense(uint64_t time, uint16_t clock, uint64_t node, NodeExpander expand)
{
	L_CALL("condense()");

	auto compacted_time = time != 0u ? ((time - UUID_TIME_INITIAL) & TIME_MASK) : time;
	auto compacted_time_clock = compacted_time & CLOCK_MASK;
	compacted_time >>= CLOCK_BITS;

	UUIDCondenser condenser;
	condenser.compact.compacted = 1u;
	condenser.compact.clock = clock ^ compacted_time_clock;
	condenser.compact.time = compacted_time;
	condenser.compact.salt = node & SALT_MASK;

	auto compacted_node = calculate_node(condenser, expand);
	if (node != compacted_node) {
		condenser.expanded.compacted = 0u;
		if ((node & MULTICAST_BIT) == 0u) {
			if (time != 0u) {
				time = (time - UUID_TIME_INITIAL) & TIME_MASK;
			}
		}
		condenser.expanded.clock = clock;
		condenser.expanded.time = time;
		condenser.expanded.node = node;
	}

	return condenser.serialise();
}


void
uncondense(const char** ptr, const char* end, uint64_t& time, uint16_t& clock, uint64_t& node, NodeExpander expand)
{
	L_CALL("uncondense({})", repr(*ptr, end));

	UUIDCondenser condenser = UUIDCondenser::unserialise(ptr, end);

	uint64_t n = condenser.compact.compacted != 0u ? calculate_node(condenser, expand) : condenser.expanded.node;

	uint64_t t = condenser.compact.time;
	if (t != 0u) {
		if (condenser.compact.compacted != 0u) {
			t = ((t << CLOCK_BITS) + UUID_TIME_INITIAL) & TIME_MASK;
		} else if ((n & MULTICAST_BIT) == 0u) {
			t = (t + UUID_TIME_INITIAL) & TIME_MASK;
		}
	}

	time = t;
	clock = static_cast<uint16_t>(condenser.compact.clock);
	node = n;
}
