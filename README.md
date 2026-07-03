# cuuid

Condensed UUID value type extracted from [Xapiand](https://github.com/Kronuz/Xapiand). It can generate, parse, format, serialise, and unserialise UUIDs, with Xapiand's compact binary wire encoding preserved byte-for-byte.

```cpp
#include "uuid.hh"

UUIDGenerator generator;
UUID uuid = generator();
std::string wire = uuid.serialise();
UUID copy = UUID::unserialise(wire);
```

## What it provides

- `UUID`, a 16-byte value wrapper with string parsing and formatting.
- `UUIDGenerator`, backed by the platform UUID API.
- Full serialisation: tag byte `0x01` plus the 16 UUID bytes.
- Condensed serialisation for RFC 4122 version-1 UUIDs when the node/time/clock fields can be packed smaller.
- `is_valid()` and `is_serialised()` validators for string and wire forms.
- UUID v1 field accessors for node, time, clock sequence, variant, and version.

## Dependencies

`cuuid` depends on two small Kronuz libraries:

- `endian` for byte-order macros (`endian.hh`).
- `char-classify` for hex parsing and rendering (`chars.hh`).

It also uses the platform UUID backend. Linux and Darwin use `uuid/uuid.h` (`UUID_LIBUUID`); FreeBSD uses `uuid.h` (`UUID_FREEBSD`).

## Build and test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Use with FetchContent

```cmake
FetchContent_Declare(cuuid GIT_REPOSITORY https://github.com/Kronuz/cuuid.git GIT_TAG main)
FetchContent_MakeAvailable(cuuid)
target_link_libraries(your_app PRIVATE cuuid::cuuid)
```

Hosts that need app-specific tracing, exceptions, or local-node salting can override the three seams with `CUUID_TRACE_HEADER`, `CUUID_EXCEPTION_HEADER`, and `CUUID_NODE_HEADER`.
