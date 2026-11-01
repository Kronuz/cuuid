# cuuid

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/cuuid/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/cuuid/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

Condensed UUIDs for C++20: 16-byte UUIDs that serialize to about 8 time-sortable bytes, with no
worker-id coordination. The condenser drops the node bits it can rebuild and writes the time
first, so the wire form sorts by creation time; the default v6 codec rebuilds the node with
splitmix64. That is half of UUIDv7 and the size of a 64-bit Snowflake. Reach for it to store
billions of ids as small, sorted keys when you control both ends of the wire.

![cuuid: a 16-byte UUID condenses to a short, time-ordered wire form and expands back](docs/cuuid.svg)

## ✨ Highlights

- **About 8 bytes on the wire** for present-day ids; the full 16-byte form is always
  available.
- **Sorts by creation time** in its condensed form, to a resolution of 1.64 ms, so it works as a
  database key.
- **v6, the default.** UUIDv6 byte order and splitmix64 node reconstruction: about 11 ns to
  encode and 12 ns to decode on an Apple M4 Pro.
- **v1, frozen.** Xapiand's original codec, byte for byte, for existing data (`Codec::v1`).
- **Fast text form.** Parses the 36-character form 7x to 9x faster than Boost.UUID.
- **Validators and accessors.** `is_valid()`, `is_serialised()`, and the v1 node, time, clock
  sequence, variant and version fields.

## 🚀 Quick start

```cpp
#include "cuuid.hh"

#include <cassert>
#include <iostream>
#include <string>

int main() {
	UUID id = cuuid::generate();                   // v6
	std::string wire = cuuid::serialise(id);       // condensed, sorts by time
	assert(cuuid::unserialise(wire) == id);
	std::cout << id << " -> " << wire.size() << " bytes\n";   // 1f1d5ec3-e6e4-6800-8d52-23423e0d9762 -> 8 bytes
}
```

The id differs on every run. [`examples/demo.cc`](examples/demo.cc) walks the codecs and the
accessors.

## 📦 Installation

Requires a C++20 compiler, CMake 3.16 or newer, and the platform UUID library (libuuid on Linux;
built in on macOS and FreeBSD); it fetches [endian](https://github.com/Kronuz/endian) and
[char-classify](https://github.com/Kronuz/char-classify) at pinned commits and compiles three
source files. CI builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus
ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(cuuid
  GIT_REPOSITORY https://github.com/Kronuz/cuuid.git
  GIT_TAG        3f0cf0aa80425d8bfc35c21f2353f7d578deca21)  # main tracks the tip
FetchContent_MakeAvailable(cuuid)

target_link_libraries(my_app PRIVATE cuuid::cuuid)
```

Then `#include "cuuid.hh"`. A host replaces logging, errors or the local node salt by defining
`CUUID_TRACE_HEADER`, `CUUID_EXCEPTION_HEADER` or `CUUID_NODE_HEADER`
([ARCHITECTURE.md](ARCHITECTURE.md#seams)).

## 🎯 When to use it

It fits stores that keep billions of ids as keys and want them small and time-sorted without
coordinating workers, where one system writes and reads the wire form.

Not for:

- **A greenfield system that must interoperate.** Use UUIDv7, or Snowflake if 64-bit ids with
  worker coordination are acceptable.
- **Order below 1.64 ms.** Ids minted within that window can sort in any order.
- **Wires of mixed codecs.** The condensed wire carries no version marker: decode with the codec
  that wrote it, `Codec::v6` by default or `Codec::v1` for data from the original codec.
- **Fast generation.** A v6 id takes the platform's v1 mint, 2.6x to 6.6x slower than Boost.UUID's
  generators.

## ⚡ Performance

ns per call, one thread, best of seven batches over 4,096 distinct ids, Apple clang 17 with `-O3`, on an
Apple M4 Pro (macOS 26, arm64) and an Intel Core i9-9980HK (macOS 15, x86-64). The comparator is
[Boost.UUID](https://www.boost.org/doc/libs/1_92_0/libs/uuid/) 1.92.0 (Homebrew),
and `cuuid_compare` checks that both libraries read the same bytes and print the same text before
it times them. Rows are the call: *parse* reads the 36-character form (`UUID{string_view}` against
`string_generator`), `to_string` writes it, and *generate* mints one id: *v6* is cuuid's default
time-ordered mint, compared with Boost's `random_generator` (random, unordered) and `time_generator_v7`
(time-ordered, the closer match); *v1 compact* is the original codec's mint, which has no Boost
counterpart and is compared with `random_generator` only for scale.

| Operation | cuuid, Apple M4 Pro | Boost.UUID, Apple M4 Pro | cuuid, Intel Core i9-9980HK | Boost.UUID, Intel Core i9-9980HK |
| :-- | --: | --: | --: | --: |
| parse the text form | 8.0 | 70.6 | 23.8 | 169.8 |
| `to_string` | 27.5 | 23.0 | 69.1 | 48.7 |
| generate, v6 | 90.9 | 13.7 (`random_generator`), 27.4 (`time_generator_v7`) | 117.1 | 17.8, 45.1 |
| generate, v1 compact | 650 | 13.7 | 788 | 17.8 |

The condensed codec has no Boost counterpart. On the M4 Pro, v6 serializes in 11 ns and
unserializes in 12 ns. v1 takes 618 and 621 ns, because a compact v1 id needs the first two
outputs of `std::mt19937(seed)`; a thread-local cache serves repeated work on one id in 10 to
11 ns.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCUUID_BENCH_BOOST=ON && cmake --build build
./build/cuuid_compare
./build/cuuid_v1_bench
```

CMake finds Boost when it is installed (it is never fetched) and links it to `cuuid_compare` only;
the version it found is in the benchmark's output. `cuuid_v1_bench` prints the v1 rows and the v6
`generate` row; the v6 codec rows are the same loop with `Codec::v6`.
[docs/COMPARISON.md](docs/COMPARISON.md) sets the codec against Snowflake, UUIDv6, UUIDv7 and
ULID.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the condenser, the wire format and the seams.
- [docs/COMPARISON.md](docs/COMPARISON.md): against Snowflake, UUIDv6, UUIDv7 and ULID, with
  benchmarks.
- [docs/CUUID_V6.md](docs/CUUID_V6.md): the v6 format.
- [docs/MIGRATION.md](docs/MIGRATION.md): rolling out v6 beside v1.
- [CHANGELOG.md](CHANGELOG.md): behavior and performance changes, newest first.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#cuuid) in Kronuz
  C++ Libraries.

## 🛠️ Building and testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
./build/cuuid_demo
```

The benchmarks build with the tests; `-DCUUID_BENCH_BOOST=ON` adds the Boost.UUID rows to
`cuuid_compare`.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC. See
[LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand).
