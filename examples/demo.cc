#include "cuuid_v1.hh"

#include <iomanip>
#include <iostream>
#include <string>

int main() {
	UUIDGenerator generator;
	UUID uuid = generator();
	std::string wire = uuid.serialise();
	UUID round_trip = UUID::unserialise(wire);

	std::cout << "uuid: " << uuid << '\n';
	std::cout << "wire bytes: " << wire.size() << '\n';
	std::cout << "wire hex:";
	for (unsigned char byte : wire) {
		std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
	}
	std::cout << std::dec << '\n';
	std::cout << "round-trip: " << (round_trip == uuid ? "ok" : "failed") << '\n';
	return round_trip == uuid ? 0 : 1;
}
