#include "cuuid.hh"
#include "cuuid_exception.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	const std::string input(reinterpret_cast<const char*>(data), size);
	const std::string_view bytes(input);
	(void)UUID::is_valid(bytes);
	(void)UUID::is_serialized(bytes);
	try {
		const UUID parsed(bytes);
		if (UUID(parsed.to_string()) != parsed) { std::abort(); }
	} catch (const cuuid::InvalidArgument&) {}
	for (auto codec : {cuuid::Codec::v1, cuuid::Codec::v6}) {
		const char* position = input.data();
		const char* end = position + input.size();
		try {
			while (position != end) {
				const char* before = position;
				const UUID value = cuuid::unserialize(&position, end, codec);
				if (position <= before || position > end) { std::abort(); }
				if (UUID(value.to_string()) != value) { std::abort(); }
			}
		} catch (const cuuid::SerializationError&) {}
		try { (void)cuuid::unserialize(bytes, codec); }
		catch (const cuuid::SerializationError&) {}
	}
	return 0;
}
