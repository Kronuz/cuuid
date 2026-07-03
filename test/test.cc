#include "uuid.hh"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
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

static void test_string_round_trip() {
	const std::string text = "3c0f2be3-ff4f-40ab-b157-c51a81eff176";
	UUID uuid(text);
	UUID same(text);
	UUID other("e47fcfdf-8db6-4469-a97f-57146dc41ced");

	CHECK(uuid.to_string() == text);
	CHECK(uuid == same);
	CHECK(uuid != other);

	const char* pos = text.data();
	const char* end = pos + text.size();
	CHECK(UUID::is_valid(&pos, end));
	CHECK(pos == end);
	CHECK(UUID::is_valid(text));
	CHECK(!UUID::is_valid("3c0f2be3-ff4f-40ab-b157-c51a81eff17x"));
}

static void test_full_serialise_round_trip() {
	UUID uuid("3c0f2be3-ff4f-40ab-b157-c51a81eff176");
	const std::string serialised = uuid.serialise();

	CHECK(serialised.size() == 17);
	CHECK(static_cast<unsigned char>(serialised[0]) == 0x01);
	CHECK(UUID::is_serialised(serialised));
	CHECK(!UUID::is_serialised(std::string_view(serialised.data(), serialised.size() - 1)));

	const char* pos = serialised.data();
	const char* end = pos + serialised.size();
	UUID decoded = UUID::unserialise(&pos, end);
	CHECK(pos == end);
	CHECK(decoded == uuid);
}

static void test_condensed_serialise_round_trip() {
	UUID uuid("00000000-0000-1000-8000-010000000000");
	const std::string serialised = uuid.serialise();

	CHECK(!serialised.empty());
	CHECK(serialised.size() < 17);
	CHECK(static_cast<unsigned char>(serialised[0]) != 0x01);
	CHECK(UUID::is_serialised(serialised));

	UUID decoded = UUID::unserialise(serialised);
	CHECK(decoded == uuid);

	std::string pair = UUID("3c0f2be3-ff4f-40ab-b157-c51a81eff176").serialise();
	pair.append(serialised);
	CHECK(UUID::is_serialised(pair));

	std::vector<UUID> decoded_pair;
	UUID::unserialise(pair, std::back_inserter(decoded_pair));
	CHECK(decoded_pair.size() == 2);
	CHECK(decoded_pair[1] == uuid);
}

static void test_uuid1_fields() {
	UUID uuid("00000000-0000-1000-8000-000000000000");

	uuid.uuid1_node(0x0123456789abULL);
	CHECK(uuid.uuid1_node() == 0x0123456789abULL);

	uuid.uuid1_time(0x0edcba987654321ULL);
	CHECK(uuid.uuid1_time() == 0x0edcba987654321ULL);

	uuid.uuid1_clock_seq(0x2345);
	CHECK(uuid.uuid1_clock_seq() == 0x2345);

	uuid.uuid_variant(0xc0);
	CHECK(uuid.uuid_variant() == 0xc0);

	uuid.uuid_version(0x0a);
	CHECK(uuid.uuid_version() == 0x0a);
}

static void test_generator() {
	UUIDGenerator generator;
	UUID uuid = generator(false);
	CHECK(!uuid.empty());
	CHECK(uuid.to_string().size() == UUID_LENGTH);
	CHECK(UUID::unserialise(uuid.serialise()) == uuid);
}

int main() {
	test_string_round_trip();
	test_full_serialise_round_trip();
	test_condensed_serialise_round_trip();
	test_uuid1_fields();
	test_generator();

	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	std::puts("all cuuid tests passed");
	return EXIT_SUCCESS;
}
