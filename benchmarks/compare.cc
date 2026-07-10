// compare.cc -- cuuid (condensed v1) vs Snowflake vs UUIDv7.
//
// Measures four things across the three ID schemes:
//   A. generation throughput
//   B. serialise + parse throughput
//   C. wire-size distribution (cuuid best/worst are time-dependent)
//   D. wire-size vs. year, to expose cuuid's time-dependence directly
//   E. byte-sortability (does lexicographic wire order match creation order?)
//
// UUIDv7 (RFC 9562) and a Twitter-style Snowflake are implemented inline so the
// benchmark is self-contained; cuuid is the real library.

#include "uuid.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;

static double ns_per(clk::duration d, std::size_t n) {
	return std::chrono::duration<double, std::nano>(d).count() / static_cast<double>(n);
}

// ---- cuuid internal time constants, mirrored from uuid.cc ----
constexpr uint64_t UUID_TIME_EPOCH   = 0x01b21dd213814000ULL;               // 1582-10-15 .. 1970 in 100ns
constexpr uint64_t UUID_TIME_YEAR    = 0x00011f0241243c00ULL;               // one year in 100ns
constexpr uint64_t UUID_TIME_INITIAL = UUID_TIME_EPOCH + (2016 - 1970) * UUID_TIME_YEAR;

// Gregorian-100ns timestamp for a given calendar year (approx, good enough for sizing).
static uint64_t greg_for_year(int year) {
	return UUID_TIME_EPOCH + static_cast<uint64_t>(year - 1970) * UUID_TIME_YEAR;
}

// Build a v1 UUID at a chosen time/clock/node.
static UUID make_v1(uint64_t greg_time, uint16_t clock, uint64_t node) {
	UUID u(std::string_view("00000000-0000-1000-8000-000000000000"));
	u.uuid_variant(0x80);
	u.uuid_version(1);
	u.uuid1_time(greg_time);
	u.uuid1_clock_seq(clock);
	u.uuid1_node(node);
	return u;
}

// ---------------- UUIDv7 (RFC 9562): 48b ms | ver | 12b rand | var | 62b rand ----------------
struct UUIDv7 {
	std::array<uint8_t, 16> b;
};

static UUIDv7 gen_v7(uint64_t ms, std::mt19937_64& rng) {
	UUIDv7 u;
	u.b[0] = static_cast<uint8_t>(ms >> 40);
	u.b[1] = static_cast<uint8_t>(ms >> 32);
	u.b[2] = static_cast<uint8_t>(ms >> 24);
	u.b[3] = static_cast<uint8_t>(ms >> 16);
	u.b[4] = static_cast<uint8_t>(ms >> 8);
	u.b[5] = static_cast<uint8_t>(ms);
	uint64_t r1 = rng();
	u.b[6] = static_cast<uint8_t>(0x70 | ((r1 >> 8) & 0x0f)); // version 7 + rand_a hi
	u.b[7] = static_cast<uint8_t>(r1 & 0xff);                 // rand_a lo
	uint64_t r2 = rng();
	u.b[8] = static_cast<uint8_t>(0x80 | ((r2 >> 56) & 0x3f)); // variant + rand_b
	for (int i = 9; i < 16; ++i) {
		u.b[i] = static_cast<uint8_t>(r2 >> ((15 - i) * 8));
	}
	return u;
}

// ---------------- UUIDv6 (RFC 9562): v1 fields, but timestamp reordered MSB-first so raw bytes sort ----------------
// The closest standard cousin to cuuid: same v1 data (Gregorian time + clock + node), made byte-sortable.
struct UUID16 {
	std::array<uint8_t, 16> b;
};

static UUID16 gen_v6(uint64_t greg60, uint16_t clock, uint64_t node) {
	UUID16 u;
	uint32_t time_high = static_cast<uint32_t>(greg60 >> 28);        // top 32 bits
	uint16_t time_mid  = static_cast<uint16_t>((greg60 >> 12) & 0xffff);
	uint16_t time_low  = static_cast<uint16_t>(greg60 & 0x0fff);     // low 12 bits
	u.b[0] = static_cast<uint8_t>(time_high >> 24);
	u.b[1] = static_cast<uint8_t>(time_high >> 16);
	u.b[2] = static_cast<uint8_t>(time_high >> 8);
	u.b[3] = static_cast<uint8_t>(time_high);
	u.b[4] = static_cast<uint8_t>(time_mid >> 8);
	u.b[5] = static_cast<uint8_t>(time_mid);
	u.b[6] = static_cast<uint8_t>(0x60 | (time_low >> 8));           // version 6 + time_low hi
	u.b[7] = static_cast<uint8_t>(time_low);
	u.b[8] = static_cast<uint8_t>(0x80 | ((clock >> 8) & 0x3f));     // variant + clock hi
	u.b[9] = static_cast<uint8_t>(clock);
	for (int i = 0; i < 6; ++i) u.b[10 + i] = static_cast<uint8_t>(node >> ((5 - i) * 8));
	return u;
}

// ---------------- ULID: 48b ms timestamp (MSB-first) + 80b randomness, byte-sortable ----------------
static UUID16 gen_ulid(uint64_t ms, std::mt19937_64& rng) {
	UUID16 u;
	for (int i = 0; i < 6; ++i) u.b[i] = static_cast<uint8_t>(ms >> ((5 - i) * 8));
	uint64_t r1 = rng(), r2 = rng();
	for (int i = 0; i < 5; ++i) u.b[6 + i] = static_cast<uint8_t>(r1 >> ((4 - i) * 8));
	for (int i = 0; i < 5; ++i) u.b[11 + i] = static_cast<uint8_t>(r2 >> ((4 - i) * 8));
	return u;
}

// ---------------- Snowflake: 41b ms-since-epoch | 10b worker | 12b seq ----------------
static uint64_t gen_snowflake(uint64_t ms, uint64_t epoch, uint32_t worker, uint32_t& seq) {
	uint64_t id = ((ms - epoch) & ((1ULL << 41) - 1)) << 22;
	id |= (static_cast<uint64_t>(worker) & 0x3ff) << 12;
	id |= (static_cast<uint64_t>(seq++) & 0xfff);
	return id;
}

static std::string snowflake_wire(uint64_t id) {
	char b[8];
	for (int i = 0; i < 8; ++i) {
		b[i] = static_cast<char>(id >> ((7 - i) * 8)); // big-endian => byte-sortable
	}
	return std::string(b, 8);
}

// Fraction of adjacent pairs already in non-decreasing lexicographic order.
static double sorted_fraction(const std::vector<std::string>& w) {
	if (w.size() < 2) return 1.0;
	std::size_t ok = 0;
	for (std::size_t i = 1; i < w.size(); ++i) {
		if (w[i - 1] <= w[i]) ++ok;
	}
	return static_cast<double>(ok) / static_cast<double>(w.size() - 1);
}

int main() {
	constexpr std::size_t N = 200000;
	std::mt19937_64 rng(0xC0FFEE);

	uint64_t now_ms = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	uint64_t now_greg = UUID_TIME_EPOCH + now_ms * 10000ULL; // ms -> 100ns

	std::printf("== cuuid vs Snowflake vs UUIDv7 == N=%zu\n\n", N);

	// ---------- A. Generation throughput ----------
	{
		UUIDGenerator g;
		volatile uint64_t sink = 0;

		auto t0 = clk::now();
		for (std::size_t i = 0; i < N; ++i) sink ^= g(false).get_bytes()[0]; // real v1, no compaction
		auto t1 = clk::now();
		for (std::size_t i = 0; i < N; ++i) sink ^= g(true).get_bytes()[0];  // real v1 + compact_crush
		auto t2 = clk::now();
		for (std::size_t i = 0; i < N; ++i) sink ^= gen_v7(now_ms, rng).b[0];
		auto t3 = clk::now();
		for (std::size_t i = 0; i < N; ++i) sink ^= gen_v6(now_greg, static_cast<uint16_t>(i), 0xABCDEF).b[0];
		auto t3b = clk::now();
		for (std::size_t i = 0; i < N; ++i) sink ^= gen_ulid(now_ms, rng).b[0];
		auto t3c = clk::now();
		uint32_t seq = 0;
		for (std::size_t i = 0; i < N; ++i) sink ^= gen_snowflake(now_ms, 1577836800000ULL, 1, seq);
		auto t4 = clk::now();

		std::printf("A. Generation (ns/op)\n");
		std::printf("   cuuid v1 (no compact)   %8.1f\n", ns_per(t1 - t0, N));
		std::printf("   cuuid v1 (compact)      %8.1f\n", ns_per(t2 - t1, N));
		std::printf("   UUIDv7                  %8.1f\n", ns_per(t3 - t2, N));
		std::printf("   UUIDv6                  %8.1f\n", ns_per(t3b - t3, N));
		std::printf("   ULID                    %8.1f\n", ns_per(t3c - t3b, N));
		std::printf("   Snowflake               %8.1f\n", ns_per(t4 - t3c, N));
		std::printf("   (sink=%llu)\n\n", static_cast<unsigned long long>(sink));
	}

	// ---------- B. Serialise + parse throughput ----------
	{
		// cuuid compact: current-time v1, compacted.
		std::vector<UUID> cu_c;
		cu_c.reserve(N);
		for (std::size_t i = 0; i < N; ++i) {
			UUID u = make_v1(now_greg, static_cast<uint16_t>(i & 0x3fff), 0);
			u.compact_crush();
			cu_c.push_back(u);
		}
		// cuuid expanded: real 48-bit node, no compaction.
		std::vector<UUID> cu_e;
		cu_e.reserve(N);
		for (std::size_t i = 0; i < N; ++i) {
			cu_e.push_back(make_v1(now_greg, static_cast<uint16_t>(i & 0x3fff),
			                       0x010000000000ULL | (rng() & 0xffffffffffULL)));
		}
		std::vector<UUIDv7> v7(N);
		for (std::size_t i = 0; i < N; ++i) v7[i] = gen_v7(now_ms, rng);
		std::vector<uint64_t> sf(N);
		{ uint32_t seq = 0; for (std::size_t i = 0; i < N; ++i) sf[i] = gen_snowflake(now_ms, 1577836800000ULL, 1, seq); }

		auto bench = [](const char* name, auto ser, auto par, std::size_t n) {
			std::vector<std::string> wires;
			wires.reserve(n);
			std::size_t bytes = 0;
			auto s0 = clk::now();
			for (std::size_t i = 0; i < n; ++i) { wires.push_back(ser(i)); bytes += wires.back().size(); }
			auto s1 = clk::now();
			volatile uint64_t sink = 0;
			for (std::size_t i = 0; i < n; ++i) sink ^= par(wires[i]);
			auto s2 = clk::now();
			std::printf("   %-22s ser %7.1f  parse %7.1f  avg_bytes %5.2f\n",
			            name, ns_per(s1 - s0, n), ns_per(s2 - s1, n),
			            static_cast<double>(bytes) / static_cast<double>(n));
			(void)sink;
		};

		std::printf("B. Serialise / parse (ns/op) and avg wire bytes\n");
		bench("cuuid compact", [&](std::size_t i){ return cu_c[i].serialise(); },
		      [&](const std::string& w){ return static_cast<uint64_t>(UUID::unserialise(w).uuid1_clock_seq()); }, N);
		bench("cuuid expanded", [&](std::size_t i){ return cu_e[i].serialise(); },
		      [&](const std::string& w){ return static_cast<uint64_t>(UUID::unserialise(w).uuid1_clock_seq()); }, N);
		bench("UUIDv7 (16B fixed)", [&](std::size_t i){ return std::string(reinterpret_cast<const char*>(v7[i].b.data()), 16); },
		      [&](const std::string& w){ uint64_t x; std::memcpy(&x, w.data(), 8); return x; }, N);
		bench("Snowflake (8B fixed)", [&](std::size_t i){ return snowflake_wire(sf[i]); },
		      [&](const std::string& w){ uint64_t x = 0; for (char c : w) x = (x << 8) | static_cast<uint8_t>(c); return x; }, N);
		std::printf("\n");
	}

	// ---------- C. cuuid wire size at current time ----------
	{
		std::size_t cmin = 99, cmax = 0, csum = 0, emin = 99, emax = 0, esum = 0;
		for (std::size_t i = 0; i < N; ++i) {
			UUID uc = make_v1(now_greg, static_cast<uint16_t>(i & 0x3fff), 0);
			uc.compact_crush();
			std::size_t cs = uc.serialise().size();
			cmin = std::min(cmin, cs); cmax = std::max(cmax, cs); csum += cs;

			UUID ue = make_v1(now_greg, static_cast<uint16_t>(i & 0x3fff), 0x010000000000ULL | (rng() & 0xffffffffffULL));
			std::size_t es = ue.serialise().size();
			emin = std::min(emin, es); emax = std::max(emax, es); esum += es;
		}
		std::printf("C. Wire size at current time (bytes)\n");
		std::printf("   cuuid compact    min %zu  max %zu  avg %.2f\n", cmin, cmax, static_cast<double>(csum) / N);
		std::printf("   cuuid expanded   min %zu  max %zu  avg %.2f\n", emin, emax, static_cast<double>(esum) / N);
		std::printf("   UUIDv7 fixed 16   Snowflake fixed 8   raw UUID 16 (+1 tag = 17)\n\n");
	}

	// ---------- D. cuuid compact wire size vs year ----------
	{
		std::printf("D. cuuid compact wire size vs. year (best->worst is time-dependent)\n");
		for (int year : {2016, 2018, 2020, 2024, 2026, 2030, 2040, 2060, 2100}) {
			UUID u = make_v1(greg_for_year(year), 0x1234, 0);
			u.compact_crush();
			std::printf("   year %4d  ->  %zu bytes\n", year, u.serialise().size());
		}
		std::printf("\n");
	}

	// ---------- E. Byte-sortability (creation order vs lexicographic wire order) ----------
	// E1: one strictly-increasing timestamp per id, spanning a wide time range (crosses the
	//     v1 time_low 32-bit wrap). Isolates "is the wire format itself time-sortable?".
	{
		constexpr std::size_t M = 50000;
		constexpr uint64_t STEP = 500000ULL; // 50ms in 100ns units -> time_low wraps ~6x over the run
		std::vector<std::string> cu_c, cu_e, v1c, v6w, v7w, ulw, sfw;
		cu_c.reserve(M); cu_e.reserve(M); v1c.reserve(M); v6w.reserve(M); v7w.reserve(M); ulw.reserve(M); sfw.reserve(M);
		uint32_t seq = 0;
		for (std::size_t i = 0; i < M; ++i) {
			uint64_t greg = now_greg + static_cast<uint64_t>(i) * STEP;    // strictly increasing, wraps time_low
			uint64_t ms   = now_ms + static_cast<uint64_t>(i);             // unique ms per id

			UUID uc = make_v1(greg, static_cast<uint16_t>(i & 0x3fff), 0);
			uc.compact_crush();
			cu_c.push_back(uc.serialise());

			UUID ue = make_v1(greg, static_cast<uint16_t>(i & 0x3fff), 0x010000000000ULL | 0xABCDEFULL);
			cu_e.push_back(ue.serialise());
			v1c.push_back(std::string(reinterpret_cast<const char*>(ue.get_bytes().data()), 16)); // canonical v1 bytes

			UUID16 u6 = gen_v6(greg, static_cast<uint16_t>(i & 0x3fff), 0xABCDEFULL);
			v6w.push_back(std::string(reinterpret_cast<const char*>(u6.b.data()), 16));

			UUIDv7 u7 = gen_v7(ms, rng);
			v7w.push_back(std::string(reinterpret_cast<const char*>(u7.b.data()), 16));

			UUID16 ul = gen_ulid(ms, rng);
			ulw.push_back(std::string(reinterpret_cast<const char*>(ul.b.data()), 16));

			sfw.push_back(snowflake_wire(gen_snowflake(ms, 1577836800000ULL, 1, seq)));
		}
		std::printf("E1. Format sortability: unique increasing timestamp per id (1.00 = wire sorts by creation time)\n");
		std::printf("   cuuid condensed wire   %.4f\n", sorted_fraction(cu_c));
		std::printf("   cuuid expanded wire    %.4f\n", sorted_fraction(cu_e));
		std::printf("   v1 canonical 16 bytes  %.4f   <- the classic v1 non-sortability\n", sorted_fraction(v1c));
		std::printf("   UUIDv6 16 bytes        %.4f\n", sorted_fraction(v6w));
		std::printf("   UUIDv7 16 bytes        %.4f\n", sorted_fraction(v7w));
		std::printf("   ULID 16 bytes          %.4f\n", sorted_fraction(ulw));
		std::printf("   Snowflake 8 bytes      %.4f\n\n", sorted_fraction(sfw));
	}

	// E2: many ids within the SAME millisecond. Exposes within-instant tie-breaking: schemes with a
	//     sub-ms counter (cuuid clock, v6 clock, snowflake seq) stay monotone; pure-random low bits
	//     (default UUIDv7 / ULID) shuffle inside a millisecond.
	{
		constexpr std::size_t M = 50000;
		std::vector<std::string> cu_c, v6w, v7w, ulw, sfw;
		cu_c.reserve(M); v6w.reserve(M); v7w.reserve(M); ulw.reserve(M); sfw.reserve(M);
		uint32_t seq = 0;
		uint64_t greg0 = now_greg;
		for (std::size_t i = 0; i < M; ++i) {
			uint64_t greg = greg0 + static_cast<uint64_t>(i) * 10ULL; // +1us: sub-ms, distinguished only by low bits/clock
			uint64_t ms   = now_ms;                                   // SAME millisecond for all

			UUID uc = make_v1(greg, static_cast<uint16_t>(i & 0x3fff), 0);
			uc.compact_crush();
			cu_c.push_back(uc.serialise());

			UUID16 u6 = gen_v6(greg, static_cast<uint16_t>(i & 0x3fff), 0xABCDEFULL);
			v6w.push_back(std::string(reinterpret_cast<const char*>(u6.b.data()), 16));

			UUIDv7 u7 = gen_v7(ms, rng);
			v7w.push_back(std::string(reinterpret_cast<const char*>(u7.b.data()), 16));

			UUID16 ul = gen_ulid(ms, rng);
			ulw.push_back(std::string(reinterpret_cast<const char*>(ul.b.data()), 16));

			sfw.push_back(snowflake_wire(gen_snowflake(ms, 1577836800000ULL, 1, seq)));
		}
		std::printf("E2. Within one millisecond: %zu ids in the same ms (0.50 ~ random order inside the instant)\n", M);
		std::printf("   cuuid condensed wire   %.4f   <- loses order below its ~1.64ms time floor (see F)\n", sorted_fraction(cu_c));
		std::printf("   UUIDv6 16 bytes        %.4f\n", sorted_fraction(v6w));
		std::printf("   UUIDv7 16 bytes        %.4f   <- default random tie-break (spec has a monotone variant)\n", sorted_fraction(v7w));
		std::printf("   ULID 16 bytes          %.4f   <- default random tie-break (spec has a monotone variant)\n", sorted_fraction(ulw));
		std::printf("   Snowflake 8 bytes      %.4f\n\n", sorted_fraction(sfw));
	}

	// F. cuuid condensed sort granularity: at what time step does the wire become fully sorted?
	//    The low 14 bits of the 100ns timestamp are XOR-folded into the clock field, so the effective
	//    sort resolution floor is 2^14 * 100ns = 1.6384 ms.
	{
		std::printf("F. cuuid condensed sort granularity (fraction ordered vs. time step between ids)\n");
		struct { const char* label; uint64_t step; } steps[] = {
			{"100 ns", 1ULL}, {"  1 us", 10ULL}, {"100 us", 1000ULL},
			{"  1 ms", 10000ULL}, {"1.64 ms", 16384ULL}, {"  2 ms", 20000ULL}, {" 10 ms", 100000ULL},
		};
		for (auto& s : steps) {
			constexpr std::size_t M = 20000;
			std::vector<std::string> w;
			w.reserve(M);
			for (std::size_t i = 0; i < M; ++i) {
				UUID u = make_v1(now_greg + static_cast<uint64_t>(i) * s.step, static_cast<uint16_t>(i & 0x3fff), 0);
				u.compact_crush();
				w.push_back(u.serialise());
			}
			std::printf("   step %-8s -> %.4f\n", s.label, sorted_fraction(w));
		}
		std::printf("\n");
	}

	// G. Optimization potential: the ~1us of the compact path is the std::mt19937 CONSTRUCTION
	//    (624-word state init) inside calculate_node(), run once per encode and once per decode.
	//    A format v2 could reconstruct the synthetic node with a cheap mixer (splitmix64) instead.
	//    This measures the node-reconstruction cost of each, in isolation.
	{
		auto fnv = [](uint64_t v) -> uint32_t {
			uint32_t h = 2166136261u;
			for (int i = 0; i < 8; ++i) { h ^= static_cast<uint8_t>(v >> (i * 8)); h *= 16777619u; }
			return h;
		};
		auto splitmix64 = [](uint64_t x) -> uint64_t {
			x += 0x9e3779b97f4a7c15ULL;
			x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
			x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
			return x ^ (x >> 31);
		};
		constexpr std::size_t M = 200000;
		volatile uint64_t sink = 0;

		auto t0 = clk::now();
		for (std::size_t i = 0; i < M; ++i) {
			uint32_t seed = fnv(i) ^ fnv(i >> 3) ^ fnv(i & 0x7f);
			std::mt19937 r(seed);                 // <-- the expensive part
			uint64_t node = (static_cast<uint64_t>(r()) << 32) | r();
			sink ^= node;
		}
		auto t1 = clk::now();
		for (std::size_t i = 0; i < M; ++i) {
			uint64_t seed = fnv(i) ^ fnv(i >> 3) ^ fnv(i & 0x7f);
			uint64_t node = splitmix64(seed);     // <-- cheap mixer
			node = (node << 32) | (splitmix64(node) >> 32);
			sink ^= node;
		}
		auto t2 = clk::now();

		std::printf("G. Node reconstruction cost (the compact path's dominant cost), ns/op\n");
		std::printf("   std::mt19937 (current wire format)  %8.1f\n", ns_per(t1 - t0, M));
		std::printf("   splitmix64   (hypothetical v2)      %8.1f\n", ns_per(t2 - t1, M));
		std::printf("   (sink=%llu)\n", static_cast<unsigned long long>(sink));
	}

	return 0;
}
