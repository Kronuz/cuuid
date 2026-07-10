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

// Umbrella entry point that dispatches between the cuuid v1 and v6 codecs. v1 and v6 are
// told apart on ENCODE by the standard UUID version nibble (1 vs 6); the condensed wire
// carries no version marker, so on DECODE the running Codec selects it (Codec::v1 is the
// pure-legacy "--legacy-ids" mode). The full serialised form (0x01 lead byte) embeds the
// 16 raw bytes and is read the same way under either codec.

#pragma once

#include <string>
#include <string_view>

#include "cuuid_v1.hh"
#include "cuuid_v6.hh"


namespace cuuid {

enum class Codec { v1, v6 };


// Encode-side dispatch by the version nibble: a version-6 UUID gets the compact v6 wire,
// everything else the v1 path (condensed if version 1, full form otherwise).
inline std::string serialise(const UUID& uuid) {
	if (uuid.uuid_variant() == 0x80 && uuid.uuid_version() == 6) {
		return cuuid_v6::serialise(uuid);
	}
	return uuid.serialise();
}


// Decode with the running codec. Only a condensed wire under Codec::v6 goes to the v6
// decoder; the full form (0x01), an empty/short buffer, and Codec::v1 all use v1's reader
// (which handles the full form, v1-condensed, and the error cases).
inline UUID unserialise(const char** ptr, const char* end, Codec codec = Codec::v6) {
	if (codec == Codec::v6 && *ptr != end && static_cast<unsigned char>(**ptr) != 0x01) {
		return cuuid_v6::unserialise(ptr, end);
	}
	return UUID::unserialise(ptr, end);
}

inline UUID unserialise(std::string_view bytes, Codec codec = Codec::v6) {
	const char* pos = bytes.data();
	const char* end = pos + bytes.size();
	return unserialise(&pos, end, codec);
}


// Mint a new id: v6 by default, v1 when Codec::v1 (pure legacy / --legacy-ids).
inline UUID generate(Codec codec = Codec::v6, bool compact = true) {
	if (codec == Codec::v6) {
		return cuuid_v6::generate();
	}
	UUIDGenerator gen;
	return gen(compact);
}

} // namespace cuuid
