// v1 condensed-wire timing: serialise / unserialise / compact generate, on one repeated id
// (the thread-local node cache hits) and on 4096 distinct ids (it misses, so this is the
// cost of the node derivation itself), plus a bare std::mt19937 construct + 2 draws and the
// v6 generate. Run the same file against two builds to compare. With the argument v6 the
// serialise, unserialise and generate rows run Codec::v6 instead of Codec::v1.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "cuuid.hh"

using clk = std::chrono::steady_clock;
static volatile unsigned long long sink;

template <class F>
static void t(const char* name, int batch, F f) {
	double best = 1e30;
	for (int r = 0; r < 7; ++r) {
		auto t0 = clk::now();
		for (int i = 0; i < batch; ++i) f(i);
		best = std::min(best, std::chrono::duration<double, std::nano>(clk::now() - t0).count() / batch);
	}
	std::printf("%-34s %9.1f ns\n", name, best);
}

int main(int argc, char** argv) {
	const bool v6 = argc > 1 && std::string_view(argv[1]) == "v6";
	const auto codec = v6 ? cuuid::Codec::v6 : cuuid::Codec::v1;
	const char* tag = v6 ? "v6" : "v1";
	auto ser = [&](const UUID& x) { return v6 ? cuuid::serialise(x) : x.serialise(); };
	auto u = cuuid::generate(codec);
	std::string w = ser(u);
	if (cuuid::unserialise(w, codec) != u) { std::puts("round-trip FAIL"); return 1; }

	std::vector<UUID> ids; std::vector<std::string> wires;
	for (int i = 0; i < 4096; ++i) { ids.push_back(cuuid::generate(codec)); wires.push_back(ser(ids.back())); }

	auto name = [&](const char* what) { static char buf[8][64]; static int n = 0; char* b = buf[n++ & 7]; std::snprintf(b, 64, "%s %s", tag, what); return b; };
	t(name("serialise (same id)"), 20000, [&](int) { auto s = ser(u); sink = s[0]; });
	t(name("unserialise (same id)"), 20000, [&](int) { auto r = cuuid::unserialise(w, codec); sink = r.get_bytes()[0]; });
	t(name("serialise (4096 distinct)"), 20000, [&](int i) { auto s = ser(ids[i & 4095]); sink = s[0]; });
	t(name("unserialise (4096 distinct)"), 20000, [&](int i) { auto r = cuuid::unserialise(wires[i & 4095], codec); sink = r.get_bytes()[0]; });
	t(name(v6 ? "generate" : "generate (compact)"), 20000, [&](int) { auto r = cuuid::generate(codec); sink = r.get_bytes()[0]; });
	if (!v6) {
		t("v1 generate (no compact)", 20000, [&](int) { auto r = cuuid::generate(cuuid::Codec::v1, false); sink = r.get_bytes()[0]; });
		t("mt19937(seed) + 2 draws", 20000, [&](int i) { std::mt19937 g(static_cast<unsigned>(i)); sink = g(); sink = g(); });
		t("v6 generate", 20000, [&](int) { auto r = cuuid::generate(); sink = r.get_bytes()[0]; });
	}
}
