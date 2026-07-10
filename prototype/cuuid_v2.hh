// cuuid_v2.hh -- PROTOTYPE of a "format v2" for cuuid. Standalone; does NOT touch
// the frozen uuid.hh / uuid.cc. Built to answer one question: does the v2 design
// (store as UUIDv6 + reconstruct the compacted node with splitmix64 + keep the
// variable-length size trick + a new tag so old ids still decode) actually work?
//
// Three deliberate departures from v1:
//   1. Canonical form is RFC 9562 UUIDv6, so the 16 raw bytes sort by time on their
//      own (v1's canonical bytes do not; that is the whole reason v6 exists).
//   2. The synthetic node of a compacted id is rebuilt with splitmix64, not a
//      std::mt19937 whose per-call construction is ~880 ns. Same job, ~2 ns, and
//      it lets the Python/JS ports delete their hand-rolled Mersenne Twister.
//   3. The condensed wire carries a distinct tag (0x02) so a v1 reader rejects it
//      and a dispatcher can route v1 (tag 0x01 / v1-condensed) and v2 side by side.
//
// The variable-length packing here favors clarity over the last byte of golf; the
// production version would fold the length into the top bits like v1's VL table.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cuuid_v2 {

// --- time base ---
// The compact wire stores the timestamp as MILLISECONDS since a 2026 epoch, not the
// 100ns Gregorian ticks v1 uses. The path already quantizes to ~1.64ms, so 100ns
// resolution was ~2 bytes of dead precision; ms + a 2026 rebase makes present-day ids
// several bytes smaller (see COMPARISON.md). The canonical v6 form still carries a full
// Gregorian timestamp (with sub-ms digits zeroed), so it stays RFC-valid and sortable.
constexpr uint64_t GREG_EPOCH_100NS = 0x01b21dd213814000ULL; // 1582-10-15 .. 1970, in 100ns
constexpr uint64_t EPOCH_2026_MS    = 1767225600000ULL;      // 2026-01-01T00:00:00Z, unix ms

constexpr uint8_t  CLOCK_BITS = 14;
constexpr uint8_t  NODE_BITS  = 48;
constexpr uint8_t  SALT_BITS  = 7;
constexpr uint64_t CLOCK_MASK = (1ULL << CLOCK_BITS) - 1;
constexpr uint64_t NODE_MASK  = (1ULL << NODE_BITS) - 1;
constexpr uint64_t SALT_MASK  = (1ULL << SALT_BITS) - 1;
constexpr uint64_t MULTICAST  = 0x010000000000ULL;

constexpr uint8_t  V2_TAG = 0x02; // distinct from v1 full (0x01); v1 reader rejects it

// Gregorian 100ns  <->  v2 ms-since-2026 (the wire's time unit).
inline uint64_t greg_to_v2ms(uint64_t greg) {
	uint64_t unix_ms = (greg - GREG_EPOCH_100NS) / 10000ULL;
	return unix_ms - EPOCH_2026_MS; // callers use times >= 2026
}
inline uint64_t v2ms_to_greg(uint64_t v2ms) {
	uint64_t unix_ms = v2ms + EPOCH_2026_MS;
	return unix_ms * 10000ULL + GREG_EPOCH_100NS;
}

// splitmix64: the cheap, deterministic mixer that replaces std::mt19937. Pure fixed-width
// integer arithmetic, so it produces identical bytes in C++, Python, and JavaScript with
// no floating point and no platform RNG. That portability is exactly what forced the old
// design to embed a hand-rolled Mersenne Twister in each port.
inline uint64_t splitmix64(uint64_t x) {
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

// A UUID as its three v1/v6 fields. Canonical serialization is v6 (sortable).
struct Id {
	uint64_t time;  // 60-bit Gregorian 100ns
	uint16_t clock; // 14-bit
	uint64_t node;  // 48-bit

	bool operator==(const Id& o) const {
		return time == o.time && clock == o.clock && node == o.node;
	}
};

// Reconstruct the synthetic node of a compacted id from the wire-native fields
// (ms-since-2026, clock, salt). Deterministic; carries the salt, sets the multicast bit.
inline uint64_t reconstruct_node(uint64_t v2ms, uint16_t clock, uint8_t salt) {
	uint64_t seed = splitmix64(v2ms ^ (static_cast<uint64_t>(clock) << 44) ^ (static_cast<uint64_t>(salt) << 57));
	uint64_t node = splitmix64(seed);
	node &= NODE_MASK & ~SALT_MASK;
	node |= salt;
	node |= MULTICAST;
	return node;
}

// Make an id's node synthetic AND normalize its timestamp to ms granularity (the v2
// equivalent of compact_crush). After this, a compact encode/decode round-trips exactly.
// Assumes time >= the 2026 epoch (a prototype simplification; production guards earlier ids).
inline void crush(Id& id) {
	uint8_t salt = static_cast<uint8_t>(id.node & SALT_MASK);
	uint64_t v2ms = greg_to_v2ms(id.time);
	id.time = v2ms_to_greg(v2ms);                 // sub-ms digits zeroed
	id.node = reconstruct_node(v2ms, id.clock, salt);
}

// ---- canonical UUIDv6 (RFC 9562): 16 raw bytes that sort by time ----
inline std::array<uint8_t, 16> to_v6_bytes(const Id& id) {
	std::array<uint8_t, 16> b{};
	uint32_t time_high = static_cast<uint32_t>((id.time >> 28) & 0xffffffff);
	uint16_t time_mid  = static_cast<uint16_t>((id.time >> 12) & 0xffff);
	uint16_t time_low  = static_cast<uint16_t>(id.time & 0x0fff);
	b[0] = static_cast<uint8_t>(time_high >> 24);
	b[1] = static_cast<uint8_t>(time_high >> 16);
	b[2] = static_cast<uint8_t>(time_high >> 8);
	b[3] = static_cast<uint8_t>(time_high);
	b[4] = static_cast<uint8_t>(time_mid >> 8);
	b[5] = static_cast<uint8_t>(time_mid);
	b[6] = static_cast<uint8_t>(0x60 | (time_low >> 8)); // version 6
	b[7] = static_cast<uint8_t>(time_low);
	b[8] = static_cast<uint8_t>(0x80 | ((id.clock >> 8) & 0x3f)); // variant + clock hi
	b[9] = static_cast<uint8_t>(id.clock);
	for (int i = 0; i < 6; ++i) b[10 + i] = static_cast<uint8_t>(id.node >> ((5 - i) * 8));
	return b;
}

inline Id from_v6_bytes(const std::array<uint8_t, 16>& b) {
	uint64_t time_high = (static_cast<uint64_t>(b[0]) << 24) | (static_cast<uint64_t>(b[1]) << 16) |
	                     (static_cast<uint64_t>(b[2]) << 8) | b[3];
	uint64_t time_mid  = (static_cast<uint64_t>(b[4]) << 8) | b[5];
	uint64_t time_low  = (static_cast<uint64_t>(b[6] & 0x0f) << 8) | b[7];
	Id id;
	id.time  = (time_high << 28) | (time_mid << 12) | time_low;
	id.clock = static_cast<uint16_t>(((b[8] & 0x3f) << 8) | b[9]);
	id.node  = 0;
	for (int i = 0; i < 6; ++i) id.node = (id.node << 8) | b[10 + i];
	return id;
}

// Order-preserving variable-length big-endian: strip leading zero bytes, then
// prefix one length byte. [len][minimal big-endian] sorts lexicographically by
// value because a larger value never has fewer bytes.
inline void put_varlen(std::string& out, uint64_t hi, uint64_t lo) {
	uint8_t buf[16];
	for (int i = 0; i < 8; ++i) buf[i] = static_cast<uint8_t>(hi >> ((7 - i) * 8));
	for (int i = 0; i < 8; ++i) buf[8 + i] = static_cast<uint8_t>(lo >> ((7 - i) * 8));
	int start = 0;
	while (start < 15 && buf[start] == 0) ++start; // keep at least one byte
	int len = 16 - start;
	out.push_back(static_cast<char>(len));
	out.append(reinterpret_cast<const char*>(buf + start), len);
}

inline void get_varlen(const uint8_t*& p, const uint8_t* end, uint64_t& hi, uint64_t& lo) {
	if (p >= end) throw std::runtime_error("v2: truncated");
	int len = *p++;
	if (len < 1 || len > 16 || p + len > end) throw std::runtime_error("v2: bad length");
	uint8_t buf[16] = {0};
	std::memcpy(buf + (16 - len), p, len);
	p += len;
	hi = lo = 0;
	for (int i = 0; i < 8; ++i) hi = (hi << 8) | buf[i];
	for (int i = 0; i < 8; ++i) lo = (lo << 8) | buf[8 + i];
}

// ---- condensed v2 wire ----
// Layout, MSB-first inside a 128-bit value so it sorts by time:
//   compact:  [ v2ms ][ clock : 14 ][ salt : 7 ][ compacted=1 : 1 ]
//   expanded: [ v2ms ][ clock : 14 ][ node : 48 ][ compacted=0 : 1 ]
// v2ms (ms since 2026) is variable width; leading zero bytes are stripped. No XOR fold:
// the timestamp is already ms-coarse, so sort granularity is 1ms with a clock tie-break.
inline std::string encode(const Id& id) {
	std::string out;
	out.push_back(static_cast<char>(V2_TAG));

	uint8_t salt = static_cast<uint8_t>(id.node & SALT_MASK);
	uint64_t v2ms = greg_to_v2ms(id.time);
	bool compact = (id.node == reconstruct_node(v2ms, id.clock, salt));

	__uint128_t v = 0;
	if (compact) {
		v = (static_cast<__uint128_t>(v2ms) << 22) |
		    (static_cast<__uint128_t>(id.clock & CLOCK_MASK) << 8) |
		    (static_cast<__uint128_t>(salt) << 1) | 1u;
	} else {
		v = (static_cast<__uint128_t>(v2ms) << 63) |
		    (static_cast<__uint128_t>(id.clock & CLOCK_MASK) << 49) |
		    (static_cast<__uint128_t>(id.node & NODE_MASK) << 1) | 0u;
	}
	put_varlen(out, static_cast<uint64_t>(v >> 64), static_cast<uint64_t>(v));
	return out;
}

inline Id decode(std::string_view wire) {
	const uint8_t* p = reinterpret_cast<const uint8_t*>(wire.data());
	const uint8_t* end = p + wire.size();
	if (p >= end || *p != V2_TAG) throw std::runtime_error("v2: not a v2 id");
	++p;
	uint64_t hi, lo;
	get_varlen(p, end, hi, lo);
	__uint128_t v = (static_cast<__uint128_t>(hi) << 64) | lo;

	Id id;
	bool compact = (static_cast<uint64_t>(v) & 1u) != 0;
	if (compact) {
		uint8_t salt  = static_cast<uint8_t>((v >> 1) & SALT_MASK);
		uint64_t clk  = static_cast<uint64_t>((v >> 8) & CLOCK_MASK);
		uint64_t v2ms = static_cast<uint64_t>(v >> 22);
		id.clock = static_cast<uint16_t>(clk);
		id.time  = v2ms_to_greg(v2ms);
		id.node  = reconstruct_node(v2ms, id.clock, salt);
	} else {
		uint64_t node = static_cast<uint64_t>((v >> 1) & NODE_MASK);
		uint64_t clk  = static_cast<uint64_t>((v >> 49) & CLOCK_MASK);
		uint64_t v2ms = static_cast<uint64_t>(v >> 63);
		id.clock = static_cast<uint16_t>(clk);
		id.node  = node;
		id.time  = v2ms_to_greg(v2ms);
	}
	return id;
}

} // namespace cuuid_v2
