// v2_demo.cc -- exercises the cuuid_v2 prototype:
//   1. round-trip correctness (compact + expanded)
//   2. codec speed vs the real (frozen) cuuid compact path
//   3. sortability of the v2 condensed wire AND the canonical v6 bytes
//   4. wire size vs the real library
//   5. coexistence: a v1 reader rejects a v2 tag; a dispatcher routes both
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

int main() {
	std::mt19937_64 rng(0xBEEF);
	uint64_t now_ms = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	uint64_t now_greg = cuuid_v2::UUID_TIME_EPOCH + now_ms * 10000ULL;

	std::printf("== cuuid v2 prototype ==\n\n");

	// ---------- 1. Round-trip correctness ----------
	{
		constexpr std::size_t N = 200000;
		std::size_t fail_c = 0, fail_e = 0;
		for (std::size_t i = 0; i < N; ++i) {
			uint64_t t = now_greg + (rng() % 3155695200000000ULL); // random within ~10y
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
		std::printf("   v2 compact total       %.2f  = 1 tag + 1 length + %.2f payload\n",
		            static_cast<double>(c_total) / M, static_cast<double>(c_total) / M - 2);
		std::printf("   v2 expanded total      %.2f  = 1 tag + 1 length + %.2f payload\n",
		            static_cast<double>(e_total) / M, static_cast<double>(e_total) / M - 2);
		std::printf("   v1 compact total       %.2f  (VL table folds length into the top bits; no tag)\n",
		            static_cast<double>(v1c_total) / M);
		std::printf("   note: v2 payload matches v1; the 2-byte framing is reclaimable (fold length, 1-bit flag)\n\n");
	}

	// ---------- 5. Coexistence ----------
	{
		UUID u1 = make_v1(now_greg, 0x1234, 0);
		u1.compact_crush();
		std::string w1 = u1.serialise();

		Id id{now_greg, 0x1234, 0x020000000000ULL | 0x55};
		cuuid_v2::crush(id);
		std::string w2 = cuuid_v2::encode(id);

		std::printf("5. Coexistence\n");
		std::printf("   v1 wire first byte = 0x%02x   v2 wire first byte = 0x%02x\n",
		            static_cast<uint8_t>(w1[0]), static_cast<uint8_t>(w2[0]));

		bool v1_rejects_v2 = false;
		try { UUID::unserialise(w2); } catch (...) { v1_rejects_v2 = true; }
		std::printf("   real cuuid decoder rejects the v2 wire: %s\n", v1_rejects_v2 ? "yes [OK]" : "no [!!]");

		// a dispatcher routes both by first byte
		auto dispatch_ok = [](const std::string& w) -> bool {
			if (!w.empty() && static_cast<uint8_t>(w[0]) == cuuid_v2::V2_TAG) {
				cuuid_v2::decode(w); return true;         // v2 path
			}
			UUID::unserialise(w); return true;            // v1 path (full or condensed)
		};
		bool routed = dispatch_ok(w1) && dispatch_ok(w2);
		std::printf("   dispatcher decodes both v1 and v2 from one reader: %s\n", routed ? "yes [OK]" : "no [!!]");
	}

	return 0;
}
