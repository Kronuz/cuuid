#include "cuuid.hh"
#include "cuuid_v6.hh"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures;                                                          \
		}                                                                        \
	} while (0)

// A tiny deterministic PRNG so the test inputs are reproducible.
static uint64_t rng_state = 0x243f6a8885a308d3ULL;
static uint64_t next_rand() {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

// A realistic 60-bit Gregorian-100ns timestamp somewhere in the 2016..2100 range, so the
// wire lands in the compact size band we care about.
static uint64_t rand_time() {
	// UUID_TIME_INITIAL (2016) plus up to ~84 years in 100ns ticks.
	const uint64_t initial = 0x01b21dd213814000ULL + (2016 - 1970) * 0x00011f0241243c00ULL;
	return (initial + (next_rand() % (84ULL * 0x00011f0241243c00ULL))) & ((1ULL << 60) - 1);
}

// A v6 UUID minted from raw fields round-trips through its condensed wire, and is a
// well-formed UUIDv6 (version 6, RFC-4122 variant).
static void test_v6_round_trip() {
	for (int i = 0; i < 20000; ++i) {
		uint64_t time = rand_time();
		uint16_t clock = static_cast<uint16_t>(next_rand() & 0x3fff);
		uint64_t node = next_rand() & 0xffffffffffffULL;

		UUID uuid = cuuid_v6::make(time, clock, node);
		CHECK(uuid.uuid_version() == 6);
		CHECK(uuid.uuid_variant() == 0x80);

		const std::string wire = cuuid_v6::serialise(uuid);
		CHECK(!wire.empty());
		CHECK(static_cast<unsigned char>(wire[0]) != 0x00); // base59-safe leading byte

		UUID decoded = cuuid_v6::unserialise(wire);
		CHECK(decoded == uuid);
	}
}

// A v6 UUID built with an arbitrary (non-reconstructable) node takes the expanded wire and
// still round-trips exactly.
static void test_v6_expanded_round_trip() {
	for (int i = 0; i < 5000; ++i) {
		uint64_t time = rand_time();
		uint16_t clock = static_cast<uint16_t>(next_rand() & 0x3fff);
		// A globally-administered (non-multicast) node: the expanded path stores it verbatim.
		uint64_t node = (next_rand() & 0xfffffeffffffULL) & ~0x010000000000ULL;

		UUID uuid = cuuid_v6::from_fields(time, clock, node);
		CHECK(uuid.uuid_version() == 6);

		UUID decoded = cuuid_v6::unserialise(cuuid_v6::serialise(uuid));
		CHECK(decoded == uuid);
	}
}

// The condensed v6 wire is byte-sortable by time (same clock/node, increasing time).
static void test_v6_sortable() {
	std::string prev;
	uint64_t base = rand_time();
	for (int i = 0; i < 50000; ++i) {
		uint64_t time = (base + static_cast<uint64_t>(i) * 500000ULL) & ((1ULL << 60) - 1);
		UUID uuid = cuuid_v6::make(time, 0x1abc, 0x0123456789abULL);
		std::string wire = cuuid_v6::serialise(uuid);
		if (!prev.empty()) {
			CHECK(prev <= wire);
		}
		prev = std::move(wire);
	}
}

// The generator mints distinct, well-formed, round-tripping v6 UUIDs.
static void test_v6_generator() {
	const int N = 2000;
	std::set<std::string> seen;
	for (int i = 0; i < N; ++i) {
		UUID uuid = cuuid_v6::generate();
		CHECK(!uuid.empty());
		CHECK(uuid.uuid_version() == 6);
		CHECK(uuid.uuid_variant() == 0x80);
		const std::string wire = cuuid_v6::serialise(uuid);
		CHECK(cuuid_v6::unserialise(wire) == uuid);
		seen.insert(wire);
	}
	CHECK(seen.size() == static_cast<size_t>(N));
}

// The umbrella dispatch routes by the version nibble on encode, and by the running codec on
// decode; v1 ids stay on the v1 path and the full form is read the same either way.
static void test_dispatch() {
	// A v6 id: cuuid::serialise picks the v6 codec.
	UUID v6 = cuuid_v6::make(rand_time(), 0x0abc, 0x0123456789abULL);
	CHECK(cuuid::serialise(v6) == cuuid_v6::serialise(v6));
	CHECK(cuuid::unserialise(cuuid::serialise(v6), cuuid::Codec::v6) == v6);

	// A v1 id: cuuid::serialise falls through to the v1 path, and decodes under Codec::v1.
	UUIDGenerator gen;
	UUID v1 = gen(true);
	CHECK(v1.uuid_version() == 1);
	CHECK(cuuid::serialise(v1) == v1.serialise());
	CHECK(cuuid::unserialise(cuuid::serialise(v1), cuuid::Codec::v1) == v1);

	// The full (uncompacted) form is self-describing: same result under either codec.
	UUID full("3c0f2be3-ff4f-40ab-b157-c51a81eff176");
	const std::string f = full.serialise();
	CHECK(static_cast<unsigned char>(f[0]) == 0x01);
	CHECK(cuuid::unserialise(f, cuuid::Codec::v1) == full);
	CHECK(cuuid::unserialise(f, cuuid::Codec::v6) == full);
}

int main() {
	test_v6_round_trip();
	test_v6_expanded_round_trip();
	test_v6_sortable();
	test_v6_generator();
	test_dispatch();

	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("all cuuid v6 tests passed");
	return EXIT_SUCCESS;
}
