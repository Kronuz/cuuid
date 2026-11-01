// cuuid_v6::generate() under threads.
//
// T threads each mint M ids at once. The platform mint is serialised inside UUIDGenerator, and
// generate() then claims max(time, last + 1) for each id with a CAS, so:
//   - every id (its 16 bytes) and every condensed wire is distinct across all threads;
//   - the claimed 100 ns time of every id is unique across all threads and strictly increasing
//     within each thread;
//   - every id is a well-formed v6 UUID that round-trips through its wire.
// The claimed time is recovered from the id by undoing make()'s compaction: the time field holds
// the time with its low 14 bits cleared, and the clock field holds those bits XOR the process's
// clock sequence.
// Run it under ThreadSanitizer for the data-race check on the claim.
#include "cuuid.hh"
#include "cuuid_common.h"
#include "cuuid_v6.hh"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			++failures;                                                          \
		}                                                                        \
	} while (0)

using u64 = std::uint64_t;

// Undo cuuid_v6::make()'s compaction: the 100 ns time the id claimed.
static u64 claimed_time(const UUID& u, uint16_t process_clock) {
	const auto& b = u.get_bytes();
	const u64 time_high = (u64(b[0]) << 24) | (u64(b[1]) << 16) | (u64(b[2]) << 8) | b[3];
	const u64 time_mid = (u64(b[4]) << 8) | b[5];
	const u64 time_low = (u64(b[6] & 0x0f) << 8) | b[7];
	const u64 time = (time_high << 28) | (time_mid << 12) | time_low;  // low 14 bits are zero
	const unsigned clock = ((b[8] & 0x3f) << 8) | b[9];
	const u64 window = ((time - UUID_TIME_INITIAL) & TIME_MASK) >> CLOCK_BITS;
	const u64 low14 = (clock ^ process_clock) & CLOCK_MASK;
	return (UUID_TIME_INITIAL + ((window << CLOCK_BITS) | low14)) & TIME_MASK;
}

int main() {
	const int T = 8;
	const int M = 4000;

	// Every call of the generator yields the process's first clock sequence.
	UUIDGenerator generator;
	const uint16_t process_clock = generator(false).uuid1_clock_seq();

	std::vector<std::vector<UUID>> ids(T);
	std::vector<std::thread> threads;
	std::atomic<int> go{0};
	for (int t = 0; t < T; ++t) {
		threads.emplace_back([&, t] {
			ids[t].reserve(M);
			while (!go.load()) {}
			for (int i = 0; i < M; ++i) ids[t].push_back(cuuid_v6::generate());
		});
	}
	go = 1;
	for (auto& th : threads) th.join();

	std::unordered_set<std::string> bytes_seen, wires_seen;
	std::unordered_set<u64> times_seen;
	u64 not_increasing = 0, duplicate_bytes = 0, duplicate_wires = 0, duplicate_times = 0, bad_round_trip = 0, malformed = 0;
	for (int t = 0; t < T; ++t) {
		u64 previous = 0;
		for (int i = 0; i < M; ++i) {
			const UUID& u = ids[t][i];
			if (u.uuid_version() != 6 || u.uuid_variant() != 0x80) ++malformed;
			const auto& raw = u.get_bytes();
			if (!bytes_seen.insert(std::string(reinterpret_cast<const char*>(raw.data()), raw.size())).second) ++duplicate_bytes;
			const std::string wire = cuuid_v6::serialise(u);
			if (!wires_seen.insert(wire).second) ++duplicate_wires;
			if (!(cuuid_v6::unserialise(wire) == u)) ++bad_round_trip;
			const u64 time = claimed_time(u, process_clock);
			if (!times_seen.insert(time).second) ++duplicate_times;
			if (i && time <= previous) ++not_increasing;
			previous = time;
		}
	}
	CHECK(malformed == 0);
	CHECK(duplicate_bytes == 0);
	CHECK(duplicate_wires == 0);
	CHECK(duplicate_times == 0);
	CHECK(not_increasing == 0);
	CHECK(bad_round_trip == 0);
	CHECK(times_seen.size() == static_cast<size_t>(T) * M);

	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed: %llu duplicate ids, %llu duplicate wires, %llu duplicate times, %llu not increasing\n",
		             failures, static_cast<unsigned long long>(duplicate_bytes), static_cast<unsigned long long>(duplicate_wires),
		             static_cast<unsigned long long>(duplicate_times), static_cast<unsigned long long>(not_increasing));
		return EXIT_FAILURE;
	}
	std::printf("all cuuid v6 thread tests passed (%d threads x %d ids)\n", T, M);
	return EXIT_SUCCESS;
}
