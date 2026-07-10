# AGENTS

Orientation for anyone working on this library. Read `ARCHITECTURE.md` for the data model and wire format.

## What this is

Condensed UUID parsing, formatting, generation, and binary serialisation extracted from Xapiand. The algorithm and wire format are a faithful port of `src/cuuid/uuid.{h,cc}`; only app couplings became seams.

## File map

```text
cuuid_common.h                  Shared condenser (UUIDCondenser + VL wire) and primitives (v1/v6).
cuuid_common.cc                 UUIDCondenser serialise/unserialise + parameterized calculate_node.
cuuid_v1.hh                     v1 UUID and UUIDGenerator public API (frozen).
cuuid_v1.cc                     v1 codec/generator; reuses cuuid_common (mt19937 node expander).
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

- **[FIXED, breaking] Node salt now uses bitwise OR.** `UUID::compact_crush()` computes the
  compacted-UUID salt as `fnv_1a((local_node_hash | node))`. The Xapiand original used a
  logical OR (`||`), which collapsed the node to a boolean before hashing, so every non-zero
  node produced the SAME salt (a compiler `-Wconstant-logical-operand` warning even flags it).
  Reproduced: over 200k distinct nodes the `||` form yields 1 distinct salt; `|` yields the full
  128-bucket spread. Fixed here. This CHANGES the salt bits of newly-compacted v1 UUIDs versus
  older versions (a deliberate breaking change for the version bump); reading previously-stored
  condensed UUIDs is unaffected (the stored salt is read back as-is).

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/cuuid_demo
./build/cuuid_benchmark
```
