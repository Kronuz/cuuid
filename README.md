# cuuid

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/cuuid/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/cuuid/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

Condensed UUIDs for C++20: standard 16-byte UUIDs that serialize to about 8
time-sortable bytes, with no worker-id coordination. That is half of UUIDv7, and the
same as a 64-bit Snowflake.

![cuuid: a 16-byte UUID condenses to a short, time-ordered wire form and back](docs/cuuid.svg)

## ✨ Highlights

- 📏 **About 8 bytes on the wire** for present-day ids; the full 16-byte form is
  always available.
- 🔢 **Sorts by creation time** in its condensed form, as a database key.
- ⚡ **v6, the default**: UUIDv6 layout and splitmix64 node reconstruction, about
  11 ns to encode and 12 ns to decode.
- 🧊 **v1, frozen**: Xapiand's original codec, byte for byte, for existing data
  (`Codec::v1`).
- 🔍 **Validators and accessors**: `is_valid()`, `is_serialised()`, and the v1 node,
  time, clock sequence, variant, and version fields.

## 📦 Installation

Requires a C++20 compiler, CMake 3.14 or newer, and the platform UUID library
(libuuid on Linux; built in on macOS and FreeBSD). It brings
[endian](https://github.com/Kronuz/endian) and
[char-classify](https://github.com/Kronuz/char-classify). CI builds it on Linux
(x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan
builds.

```cmake
include(FetchContent)
FetchContent_Declare(cuuid
  GIT_REPOSITORY https://github.com/Kronuz/cuuid.git
  GIT_TAG        main)
FetchContent_MakeAvailable(cuuid)

target_link_libraries(my_app PRIVATE cuuid::cuuid)
```

## 🚀 Quick start

```cpp
#include "cuuid.hh"

#include <cassert>
#include <iostream>

int main() {
	UUID id = cuuid::generate();                   // v6
	std::string wire = cuuid::serialise(id);       // ~8 bytes, sorts by time
	assert(cuuid::unserialise(wire) == id);
	std::cout << id << " -> " << wire.size() << " bytes\n";
}
```

Decoding uses the codec you run: `Codec::v6` by default, or `Codec::v1` for data
written by the original codec. The condensed wire carries no version marker.

## ⚙️ Configuration

A host can replace three seams by defining a header before the build:
`CUUID_TRACE_HEADER` (logging), `CUUID_EXCEPTION_HEADER` (errors), and
`CUUID_NODE_HEADER` (the local node salt).

## 🎯 When to use it

- ✅ Billions of ids stored as keys, small and time-sorted, without coordination,
  where you control both ends of the wire.
- ❌ A greenfield system that must interoperate: use UUIDv7. Or use Snowflake if
  64-bit ids with worker coordination are acceptable.

## ⚡ Performance

Apple M4 Pro, Apple clang 17 with `-O3 -DNDEBUG -std=gnu++20`, measured under
`quiet strict` at `d03d8d1`. Each figure is ns per call, the best of 7 batches of 20,000
calls over 4,096 distinct ids:

| Operation | v6 | v1 |
|---|---:|---:|
| `serialise` | 11 | 618 |
| `unserialise` | 12 | 621 |
| `generate` (compact) | 91 | 667 |

A compact v1 id needs the first two outputs of `std::mt19937(seed)`.
`expand_mt_compute` computes only the 399 state words those depend on, and a small
thread-local cache serves repeated work on one id: a repeated v1 `serialise` takes
10 ns and `unserialise` 11 ns. A bare `std::mt19937(seed)` with two draws takes 919 ns.
v6 reconstructs the node with splitmix64 instead.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/cuuid_v1_bench
```

`cuuid_v1_bench` prints the v1 rows and the v6 `generate` row. The v6 `serialise` and
`unserialise` rows are the same loop with `Codec::v6`.
[docs/COMPARISON.md](docs/COMPARISON.md) sets the codec against Snowflake, UUIDv6,
UUIDv7, and ULID.

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the condenser and the wire format.
- [docs/COMPARISON.md](docs/COMPARISON.md): against Snowflake, UUIDv6, UUIDv7, and
  ULID, with benchmarks.
- [docs/CUUID_V6.md](docs/CUUID_V6.md): the v6 format.
- [docs/MIGRATION.md](docs/MIGRATION.md): rolling out v6 beside v1.
- [AGENTS.md](AGENTS.md): repository map and invariants.
- [CHANGELOG.md](CHANGELOG.md): behavior and performance changes.

## 🛠️ Building and testing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build
./build/cuuid_demo && ./build/cuuid_compare
```

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
