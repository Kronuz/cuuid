# AGENTS

Orientation for anyone working on this library. Read `ARCHITECTURE.md` for the data model and wire format.

## What this is

Condensed UUID parsing, formatting, generation, and binary serialisation extracted from Xapiand. The algorithm and wire format are a faithful port of `src/cuuid/uuid.{h,cc}`; only app couplings became seams.

## File map

```text
uuid.hh                         UUID and UUIDGenerator public API.
uuid.cc                         Verbatim codec/generator implementation with seams.
cuuid_trace.h                   No-op L_* logging defaults, override with CUUID_TRACE_HEADER.
cuuid_exception.h               std-based THROW defaults, override with CUUID_EXCEPTION_HEADER.
cuuid_node.h                    local_node_hash() default, override with CUUID_NODE_HEADER.
examples/demo.cc                Generate, serialise, unserialise, and print a UUID.
test/test.cc                    ctest coverage for string/wire round-trips and v1 fields.
benchmarks/serialise_parse.cc   Small serialise/parse throughput harness.
```

## Dependencies

`endian` (`endian.hh`) and `char-classify` (`chars.hh`) are real dependencies. The platform UUID backend comes from `uuid/uuid.h` on Linux/Darwin or `uuid.h` on FreeBSD.

## Invariants

- **Wire format is byte-identical.** Do not change the full tag (`0x01`), the condensed bit layout, byte order, or the variable-length prefix table during extraction work.
- **Keep the algorithms faithful.** No cleanup, optimization, or behavior change belongs in the extraction commit.
- **Public API stays global.** `UUID`, `UUIDGenerator`, and `UUID_LENGTH` remain in the global namespace so Xapiand only needs include swaps.
- **Seams stay compile-time and no-op by default.** A standalone consumer must not need Xapiand's logger, exception hierarchy, or `Node` registry.

## Invariants / suspected pre-existing bugs

- **Node salt uses logical OR.** `UUID::compact_crush()` hashes `(local_node_hash || node)`. That is a logical OR, not a bitwise OR, so most non-zero inputs collapse to `true` before hashing. This is almost certainly suspicious, but it is preserved verbatim to avoid changing compacted UUID output during extraction. Review and fix separately if desired.

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/cuuid_demo
./build/cuuid_benchmark
```
