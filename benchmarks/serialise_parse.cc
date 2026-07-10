#include "cuuid_v1.hh"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
	constexpr std::size_t count = 100000;
	std::vector<UUID> uuids;
	uuids.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		UUID uuid("00000000-0000-1000-8000-010000000000");
		uuid.uuid1_time(static_cast<uint64_t>(i + 1));
		uuid.uuid1_clock_seq(static_cast<uint16_t>(i & 0x3fff));
		uuid.uuid1_node(0x010000000000ULL | (static_cast<uint64_t>(i) & 0xffffffffULL));
		uuids.push_back(uuid);
	}

	std::vector<std::string> wires;
	wires.reserve(count);
	auto start = std::chrono::steady_clock::now();
	std::size_t total_bytes = 0;
	for (const auto& uuid : uuids) {
		wires.push_back(uuid.serialise());
		total_bytes += wires.back().size();
	}
	auto mid = std::chrono::steady_clock::now();

	std::size_t checksum = 0;
	for (const auto& wire : wires) {
		checksum += UUID::unserialise(wire).uuid1_clock_seq();
	}
	auto end = std::chrono::steady_clock::now();

	auto serialise_us = std::chrono::duration_cast<std::chrono::microseconds>(mid - start).count();
	auto parse_us = std::chrono::duration_cast<std::chrono::microseconds>(end - mid).count();
	std::cout << "uuids=" << count << " bytes=" << total_bytes << " checksum=" << checksum << '\n';
	std::cout << "serialise_us=" << serialise_us << " parse_us=" << parse_us << '\n';
	return 0;
}
