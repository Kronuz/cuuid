// v2_demo.cc -- exercises the cuuid_v2 prototype:
//   1. round-trip correctness (compact + expanded)
//   2. codec speed vs the real (frozen) cuuid compact path
//   3. sortability of the v2 condensed wire AND the canonical v6 bytes
//   4. wire size vs the real library
//   5. coexistence: v1 and v2 both keep a nonzero first byte (base59-safe); versioned out of band
//
// Links the real cuuid (uuid.hh) for the head-to-head; the prototype is header-only.

#include "uuid.hh"
#include "../prototype/cuuid_v2.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

using clk = std::chrono::steady_clock;
using cuuid_v2::Id;

static double ns_per(clk::duration d, std::size_t n) {
	return std::chrono::duration<double, std::nano>(d).count() / static_cast<double>(n);
}

static double sorted_fraction(const std::vector<std::string>& w) {
	if (w.size() < 2) return 1.0;
	std::size_t ok = 0;
	for (std::size_t i = 1; i < w.size(); ++i) if (w[i - 1] <= w[i]) ++ok;
	return static_cast<double>(ok) / static_cast<double>(w.size() - 1);
}

// Build a real cuuid v1 UUID at a chosen time/clock/node (mirrors compare.cc).
static UUID make_v1(uint64_t greg_time, uint16_t clock, uint64_t node) {
	UUID u(std::string_view("00000000-0000-1000-8000-000000000000"));
	u.uuid_variant(0x80);
	u.uuid_version(1);
	u.uuid1_time(greg_time);
	u.uuid1_clock_seq(clock);
	u.uuid1_node(node);
	return u;
}

int main(int argc, char** argv) {
	std::mt19937_64 rng(0xBEEF);
	uint64_t now_ms = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	uint64_t now_greg = cuuid_v2::GREG_EPOCH_100NS + now_ms * 10000ULL;

	// --vectors: emit cross-language test vectors (the Python/JS ports validate against these).
	// Each line: mode<TAB>time<TAB>clock<TAB>node<TAB>wire_hex<TAB>v6_hex
	// where (time,clock,node) are the final fields (post-crush for compact).
	if (argc > 1 && std::string(argv[1]) == "--vectors") {
		auto hex = [](const std::string& s) {
			static const char* H = "0123456789abcdef";
			std::string o;
			for (unsigned char c : s) { o.push_back(H[c >> 4]); o.push_back(H[c & 0xf]); }
			return o;
		};
		auto emit = [&](const char* mode, cuuid_v2::Id id) {
			std::string w = cuuid_v2::encode(id);
			auto b = cuuid_v2::to_v6_bytes(id);
			std::string v6(reinterpret_cast<const char*>(b.data()), 16);
			std::printf("%s\t%llu\t%u\t%llu\t%s\t%s\n", mode,
			            static_cast<unsigned long long>(id.time), id.clock,
			            static_cast<unsigned long long>(id.node), hex(w).c_str(), hex(v6).c_str());
		};
		uint64_t yr_ms = 31556952000ULL;
		for (uint64_t years : {0ULL, 1ULL, 4ULL, 9ULL, 24ULL, 74ULL}) {
			uint64_t ms = cuuid_v2::EPOCH_2026_MS + years * yr_ms + 123456ULL;
			uint64_t greg = cuuid_v2::GREG_EPOCH_100NS + ms * 10000ULL;
			for (uint16_t clock : {uint16_t(0), uint16_t(1), uint16_t(0x2abc & 0x3fff)}) {
				cuuid_v2::Id c{greg, clock, 0x123456789aULL};
				cuuid_v2::crush(c);
				emit("compact", c);
				emit("expanded", cuuid_v2::Id{greg, clock, 0x02aabbccddeeULL});
			}
		}
		return 0;
	}

	// --compat: how do v1 and v2 wires relate on the wire? base59 (the "~" text form) forbids a
	// leading zero, so v2 cannot use a leading tag byte; its first byte, like v1's, is a nonzero
	// length byte. This probe shows the two first-byte spaces OVERLAP, so v1-vs-v2 cannot be
	// dispatched by the first byte and must be versioned out of band (index/schema) or a trailing bit.
	if (argc > 1 && std::string(argv[1]) == "--compat") {
		std::set<int> v1_fb, v2_fb;
		std::size_t v2_leading_zero = 0, total = 0;
		uint64_t yr = 315569520000ULL; // ~1y in ms
		std::mt19937_64 r(0xD00D);
		for (uint64_t ymul = 0; ymul < 90; ++ymul) {
			for (int rep = 0; rep < 4000; ++rep) {
				uint64_t ms = (ymul * yr) + (r() % yr);
				uint64_t greg = cuuid_v2::GREG_EPOCH_100NS + ms * 10000ULL;
				uint16_t clk = static_cast<uint16_t>(r() & 0x3fff);

				UUID uc = make_v1(greg, clk, 0x010000000000ULL | (r() & 0xffffffffffULL));
				uc.compact_crush();
				v1_fb.insert(static_cast<uint8_t>(uc.serialise()[0]));

				cuuid_v2::Id id{greg, clk, 0x020000000000ULL | (r() & 0xffffffffffULL)};
				cuuid_v2::crush(id);
				std::string w2 = cuuid_v2::encode(id);
				int b0 = static_cast<uint8_t>(w2[0]);
				v2_fb.insert(b0);
				if (b0 == 0) ++v2_leading_zero;
				++total;
			}
		}
		std::set<int> overlap;
		for (int b : v2_fb) if (v1_fb.count(b)) overlap.insert(b);
		std::printf("== v1 / v2 first-byte relationship (%zu ids each) ==\n", total);
		std::printf("   v1 distinct first bytes: %zu   v2 distinct first bytes: %zu   overlap: %zu\n",
		            v1_fb.size(), v2_fb.size(), overlap.size());
		std::printf("   v2 wires that start with 0x00 (base59-unsafe): %zu %s\n",
		            v2_leading_zero, v2_leading_zero ? "[BUG]" : "[OK, all nonzero]");
		std::printf("   -> first bytes overlap, so v1-vs-v2 must be versioned OUT OF BAND,\n");
		std::printf("      not by a leading tag byte (which base59 would drop anyway).\n");
		return 0;
	}

	// --v1-vectors: emit the REAL v1 (current library) forms for a fixed set of inputs, so the
	// Python side (compare_v1.py) can print v1 and v2 side by side with base59 "~" strings.
	// line: time<TAB>clock<TAB>node<TAB>v1_canonical<TAB>v1_wire_hex
	if (argc > 1 && std::string(argv[1]) == "--v1-vectors") {
		auto hex = [](const std::string& s) {
			static const char* H = "0123456789abcdef";
			std::string o;
			for (unsigned char c : s) { o.push_back(H[c >> 4]); o.push_back(H[c & 0xf]); }
			return o;
		};
		uint64_t yr_ms = 31556952000ULL;
		uint64_t base_ms = cuuid_v2::EPOCH_2026_MS + 190ULL * 86400 * 1000;
		struct { const char* lbl; uint64_t off_ms; uint16_t clock; uint64_t node; } cases[] = {
			{"A", 0, 0x1abc, 0x0123456789abULL},
			{"B", 1, 0x1abc, 0x0123456789abULL},
			{"C", 9 * yr_ms, 0x0042, 0xfedcba987654ULL},
		};
		for (auto& c : cases) {
			uint64_t greg = cuuid_v2::GREG_EPOCH_100NS + (base_ms + c.off_ms) * 10000ULL;
			UUID u = make_v1(greg, c.clock, c.node);
			u.compact_crush();
			std::printf("%llu\t%u\t%llu\t%s\t%s\n",
			            static_cast<unsigned long long>(greg), c.clock,
			            static_cast<unsigned long long>(c.node),
			            u.to_string().c_str(), hex(u.serialise()).c_str());
		}
		return 0;
	}

	std::printf("== cuuid v2 prototype ==\n\n");

	// ---------- 1. Round-trip correctness ----------
	{
		constexpr std::size_t N = 200000;
		std::size_t fail_c = 0, fail_e = 0;
		for (std::size_t i = 0; i < N; ++i) {
			uint64_t t = now_greg + (rng() % 315569520000ULL) * 10000ULL; // random ms within ~10y, ms-aligned
			uint16_t c = static_cast<uint16_t>(rng() & 0x3fff);
			uint64_t n = 0x020000000000ULL | (rng() & 0xffffffffffULL);

			Id comp{t, c, n};
			cuuid_v2::crush(comp);                       // normalize to stored form
			Id back_c = cuuid_v2::decode(cuuid_v2::encode(comp));
			if (!(back_c == comp)) ++fail_c;

			Id exp{t, c, n};                             // expanded: keep the real node
			Id back_e = cuuid_v2::decode(cuuid_v2::encode(exp));
			if (!(back_e == exp)) ++fail_e;

			// canonical v6 round-trip too
			if (!(cuuid_v2::from_v6_bytes(cuuid_v2::to_v6_bytes(exp)) == exp)) ++fail_e;
		}
		std::printf("1. Round-trip over %zu ids: compact fails=%zu, expanded fails=%zu  %s\n\n",
		            N, fail_c, fail_e, (fail_c == 0 && fail_e == 0) ? "[OK]" : "[FAIL]");
	}

	// ---------- 1b. VL-folded wire: round-trip, size (8 bytes), sortability ----------
	{
		constexpr std::size_t N = 200000;
		std::size_t fail_c = 0, fail_e = 0, csize = 0, esize = 0;
		std::vector<std::string> sorted_run;
		for (std::size_t i = 0; i < N; ++i) {
			uint64_t t = now_greg + (rng() % 315569520000ULL) * 10000ULL;
			uint16_t c = static_cast<uint16_t>(rng() & 0x3fff);
			uint64_t n = 0x020000000000ULL | (rng() & 0xffffffffffULL);
			Id comp{t, c, n};
			cuuid_v2::crush(comp);
			std::string wc = cuuid_v2::encode_vl(comp);
			if (!(cuuid_v2::decode_vl(wc) == comp)) ++fail_c;
			csize += wc.size();
			Id exp{t, c, n};
			std::string we = cuuid_v2::encode_vl(exp);
			if (!(cuuid_v2::decode_vl(we) == exp)) ++fail_e;
			esize += we.size();
		}
		// sortability: strictly increasing time -> VL wire must sort
		for (std::size_t i = 0; i < 50000; ++i) {
			Id id{now_greg + i * 500000ULL, static_cast<uint16_t>(i & 0x3fff), 0};
			cuuid_v2::crush(id);
			sorted_run.push_back(cuuid_v2::encode_vl(id));
		}
		std::printf("1b. VL-folded wire (final format): compact fails=%zu, expanded fails=%zu  %s\n",
		            fail_c, fail_e, (fail_c == 0 && fail_e == 0) ? "[OK]" : "[FAIL]");
		std::printf("    avg size compact %.2f B (was 9 with a length byte), expanded %.2f B; sortable %.4f\n\n",
		            static_cast<double>(csize) / N, static_cast<double>(esize) / N, sorted_fraction(sorted_run));
	}

	// ---------- 2. Codec speed vs real cuuid compact ----------
	{
		constexpr std::size_t N = 200000;
		std::vector<Id> ids;
		ids.reserve(N);
		std::vector<UUID> v1;
		v1.reserve(N);
		for (std::size_t i = 0; i < N; ++i) {
			Id id{now_greg, static_cast<uint16_t>(i & 0x3fff), 0x020000000000ULL | (rng() & 0xffffffffffULL)};
			cuuid_v2::crush(id);
			ids.push_back(id);
			UUID u = make_v1(now_greg, static_cast<uint16_t>(i & 0x3fff), 0);
			u.compact_crush();
			v1.push_back(u);
		}

		std::vector<std::string> w2, w1;
		w2.reserve(N); w1.reserve(N);
		auto a0 = clk::now();
		for (auto& id : ids) w2.push_back(cuuid_v2::encode(id));
		auto a1 = clk::now();
		for (auto& u : v1) w1.push_back(u.serialise());
		auto a2 = clk::now();
		volatile uint64_t sink = 0;
		for (auto& w : w2) sink ^= cuuid_v2::decode(w).clock;
		auto a3 = clk::now();
		for (auto& w : w1) sink ^= UUID::unserialise(w).uuid1_clock_seq();
		auto a4 = clk::now();

		std::printf("2. Codec speed (ns/op)\n");
		std::printf("   v2 encode (splitmix64)   %8.1f     v1 encode (mt19937)   %8.1f\n", ns_per(a1 - a0, N), ns_per(a2 - a1, N));
		std::printf("   v2 decode (splitmix64)   %8.1f     v1 decode (mt19937)   %8.1f\n", ns_per(a3 - a2, N), ns_per(a4 - a3, N));
		std::printf("   (sink=%llu)\n\n", static_cast<unsigned long long>(sink));
	}

	// ---------- 3 & 4. Sortability and size ----------
	{
		constexpr std::size_t M = 50000;
		constexpr uint64_t STEP = 500000ULL; // 50ms, crosses v1 time_low wraps
		std::vector<std::string> v2_wire, v2_v6;
		v2_wire.reserve(M); v2_v6.reserve(M);
		std::size_t c_total = 0, e_total = 0, v1c_total = 0;
		for (std::size_t i = 0; i < M; ++i) {
			uint64_t t = now_greg + static_cast<uint64_t>(i) * STEP;
			Id id{t, static_cast<uint16_t>(i & 0x3fff), 0x020000000000ULL | 0xABCDEFULL};
			cuuid_v2::crush(id);
			std::string w = cuuid_v2::encode(id);
			v2_wire.push_back(w);
			auto b = cuuid_v2::to_v6_bytes(id);
			v2_v6.push_back(std::string(reinterpret_cast<const char*>(b.data()), 16));
			c_total += w.size();

			Id ex{t, static_cast<uint16_t>(i & 0x3fff), 0x020000000000ULL | (0xCAFE00 + i)};
			e_total += cuuid_v2::encode(ex).size();

			UUID u1 = make_v1(t, static_cast<uint16_t>(i & 0x3fff), 0);
			u1.compact_crush();
			v1c_total += u1.serialise().size();
		}
		std::printf("3. Sortability (1.0 = wire sorts by creation time)\n");
		std::printf("   v2 condensed wire      %.4f\n", sorted_fraction(v2_wire));
		std::printf("   v2 canonical v6 bytes  %.4f   <- v1 canonical is NOT sortable; v6 is\n\n", sorted_fraction(v2_v6));
		std::printf("4. Wire size at current time (bytes)\n");
		std::printf("   v2 compact total       %.2f  = 1 length + %.2f payload (no leading tag)\n",
		            static_cast<double>(c_total) / M, static_cast<double>(c_total) / M - 1);
		std::printf("   v2 expanded total      %.2f  = 1 length + %.2f payload\n",
		            static_cast<double>(e_total) / M, static_cast<double>(e_total) / M - 1);
		std::printf("   v1 compact total       %.2f  (100ns/2016; VL folds length, no tag)\n",
		            static_cast<double>(v1c_total) / M);
		std::printf("   v2 compact PAYLOAD vs year (ms/2026 shrinks the time field ~2 bytes vs 100ns):\n");
		uint64_t yr_ms = 31556952000ULL;
		for (uint64_t years : {0ULL, 1ULL, 4ULL, 9ULL, 24ULL, 74ULL}) {
			uint64_t ms = cuuid_v2::EPOCH_2026_MS + years * yr_ms + 500ULL;
			uint64_t greg = cuuid_v2::GREG_EPOCH_100NS + ms * 10000ULL;
			Id id{greg, 0x1abc, 0x33ULL};
			cuuid_v2::crush(id);
			std::printf("      %4llu (year %llu)  payload %zu bytes\n",
			            static_cast<unsigned long long>(2026 + years),
			            static_cast<unsigned long long>(years),
			            cuuid_v2::encode(id).size() - 1);
		}
		std::printf("\n");
	}

	// ---------- 5. Coexistence / base59-safety ----------
	{
		UUID u1 = make_v1(now_greg, 0x1234, 0);
		u1.compact_crush();
		std::string w1 = u1.serialise();

		Id id{now_greg, 0x1234, 0x020000000000ULL | 0x55};
		cuuid_v2::crush(id);
		std::string w2 = cuuid_v2::encode(id);

		std::printf("5. Coexistence / base59-safety\n");
		std::printf("   v1 wire first byte = 0x%02x   v2 wire first byte = 0x%02x\n",
		            static_cast<uint8_t>(w1[0]), static_cast<uint8_t>(w2[0]));
		std::printf("   both first bytes nonzero (base59 '~' text form safe): %s\n",
		            (static_cast<uint8_t>(w1[0]) && static_cast<uint8_t>(w2[0])) ? "yes [OK]" : "no [!!]");
		std::printf("   v1 and v2 both look 'condensed' on the wire, so v1-vs-v2 is versioned\n");
		std::printf("   OUT OF BAND (index/schema), not by the first byte. (See --compat.)\n");
	}

	return 0;
}

