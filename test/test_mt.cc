// Differential tests for the v1 node expander: the shortcut that computes only the
// std::mt19937 state words the first two outputs depend on must equal the real engine,
// and every v1 wire byte must equal the one the original (full engine) expander gives.
#include "cuuid_common.h"
#include "cuuid_v1.hh"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures;                                                          \
		}                                                                        \
	} while (0)

// Frozen copy of the original expander.
static uint64_t reference_expand(uint32_t seed) {
	std::mt19937 rng(seed);
	uint64_t node = rng();
	node <<= 32;
	node |= rng();
	return node;
}

static void test_expand_matches_engine() {
	// Edge seeds.
	for (uint32_t seed : {0u, 1u, 2u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu, 5489u, 624u, 397u, 398u}) {
		CHECK(expand_mt_compute(seed) == reference_expand(seed));
	}
	// A dense low range, every bit position flipped from several bases, and random seeds.
	for (uint32_t seed = 0; seed < 20000; ++seed) {
		CHECK(expand_mt_compute(seed) == reference_expand(seed));
	}
	for (uint32_t base : {0u, 0xffffffffu, 0xa5a5a5a5u, 0x12345678u}) {
		for (int b = 0; b < 32; ++b) {
			uint32_t seed = base ^ (1u << b);
			CHECK(expand_mt_compute(seed) == reference_expand(seed));
		}
	}
	std::mt19937_64 gen(0xc0ffee);
	for (int i = 0; i < 300000; ++i) {
		uint32_t seed = static_cast<uint32_t>(gen());
		CHECK(expand_mt_compute(seed) == reference_expand(seed));
	}
}

static void test_wire_identical() {
	std::mt19937_64 gen(42);
	const uint64_t initial = 0x01b21dd213814000ULL + (2016 - 1970) * 0x00011f0241243c00ULL;
	for (int i = 0; i < 50000; ++i) {
		uint64_t time = (initial + gen() % (84ULL * 0x00011f0241243c00ULL)) & ((1ULL << 60) - 1);
		uint16_t clock = static_cast<uint16_t>(gen() & 0x3fff);
		uint64_t node = gen() & 0xffffffffffffULL;

		UUID uuid;
		uuid.uuid1_time(time);
		uuid.uuid1_clock_seq(clock);
		uuid.uuid1_node(node);
		uuid.uuid_variant(0x80);
		uuid.uuid_version(1);

		const std::string wire = uuid.serialise();
		CHECK(wire == condense(time, clock, node, reference_expand));

		// Serialising again (cache hit) and decoding give the same bytes / value.
		CHECK(uuid.serialise() == wire);
		UUID back = UUID::unserialise(wire);
		const char* p = wire.data();
		uint64_t t2, n2; uint16_t c2;
		uncondense(&p, wire.data() + wire.size(), t2, c2, n2, reference_expand);
		UUID ref;
		ref.uuid1_time(t2); ref.uuid1_clock_seq(c2); ref.uuid1_node(n2);
		ref.uuid_variant(0x80); ref.uuid_version(1);
		CHECK(back == ref);

		// compact_crush agrees with the reference crush.
		UUID crushed = uuid;
		crushed.compact_crush();
		uint64_t ct = time, cn = node; uint16_t cc = clock;
		crush_fields(ct, cc, cn, reference_expand);
		UUID want;
		want.uuid1_time(ct); want.uuid1_clock_seq(cc); want.uuid1_node(cn);
		want.uuid_variant(0x80); want.uuid_version(1);
		CHECK(crushed == want);
	}
}

int main() {
	test_expand_matches_engine();
	test_wire_identical();
	if (failures) {
		std::fprintf(stderr, "%d failures\n", failures);
		return 1;
	}
	std::printf("cuuid mt expander tests passed\n");
	return 0;
}
