# AGENTS

Orientation for anyone working on this library. Read `ARCHITECTURE.md` for the data model and wire format.

## What this is

Condensed UUID parsing, formatting, generation, and binary serialisation extracted from Xapiand. The wire format matches Xapiand's `src/cuuid/uuid.{h,cc}` (apart from the node salt, below); app couplings are compile-time seams.

## File map

```text
cuuid_common.h                  Shared condenser (UUIDCondenser + VL wire) and primitives (v1/v6).
cuuid_common.cc                 UUIDCondenser serialise/unserialise + parameterized calculate_node.
cuuid_v1.hh                     v1 UUID and UUIDGenerator public API (frozen).
cuuid_v1.cc                     v1 codec/generator; reuses cuuid_common (mt19937 node expander).
cuuid_v6.hh                     v6 codec API (make/generate/serialise/unserialise) on the shared UUID.
cuuid_v6.cc                     v6: splitmix64 expander + RFC-9562 UUIDv6 layout; reuses cuuid_common.
cuuid.hh                        Umbrella: version-nibble encode dispatch + running-codec decode.
cuuid_trace.h                   No-op L_* logging defaults, override with CUUID_TRACE_HEADER.
cuuid_exception.h               std-based THROW defaults, override with CUUID_EXCEPTION_HEADER.
cuuid_node.h                    local_node_hash() default, override with CUUID_NODE_HEADER.
examples/demo.cc                Generate, serialise, unserialise, and print a UUID.
test/test.cc                    ctest coverage for string/wire round-trips and v1 fields.
test/test_mt.cc                 Differential test: fast v1 node expander vs std::mt19937 and frozen full-engine wire bytes.
test/test_v6.cc                 ctest coverage for v6 round-trips, UUIDv6 layout, wire sort, canonical byte order by 1.64 ms window, dispatch.
test/test_v6_threads.cc         ctest `cuuid_v6_threads`: 8 threads x 4000 `cuuid_v6::generate()` ids are distinct, with unique claimed times that increase within each thread; run under TSan.
benchmarks/serialise_parse.cc   Small serialise/parse throughput harness.
benchmarks/v1_bench.cc          v1 serialise/unserialise/generate ns per op (cuuid_v1_bench).
benchmarks/compare.cc           cuuid against Snowflake, UUIDv6, UUIDv7, ULID (cuuid_compare); with -DCUUID_BENCH_BOOST=ON also Boost.UUID text parse, text form and generation (Boost is found, never fetched).
prototype/                      v6 prototypes, Python/JS ports, cross-validation; v2_demo.cc builds as cuuid_v2_demo.
docs/COMPARISON.md, CUUID_V6.md, MIGRATION.md
                                Comparison, the v6 format, and the v6 rollout plan.
docs/cuuid.d2                   README diagram; render: d2 --layout elk --sketch --theme 302 docs/cuuid.d2 docs/cuuid.svg
```

## Dependencies

`endian` (`endian.hh`) and `char-classify` (`chars.hh`) are real dependencies. The platform UUID backend comes from `uuid/uuid.h` on Linux/Darwin or `uuid.h` on FreeBSD.

## Invariants

- **Wire format is byte-identical.** Do not change the full tag (`0x01`), the condensed bit layout, byte order, or the variable-length prefix table.
- **Optimizations keep the wire bytes.** A faster path must produce the same bytes and prove it with a differential test, as `test/test_mt.cc` does for the v1 node expander.
- **Canonical v6 bytes sort by time at the compaction window, 2^14 ticks of 100 ns (1.64 ms).** `make()` and `generate()` run `crush_fields`, which clears the low 14 bits of the time and folds them into the clock field, so ids in one window are distinct but not in creation order. `test_v6_canonical_order` pins it; don't document a finer granularity.
- **v6 `generate()` claims `max(time, last + 1)` with a CAS, and is not lock-free.** The CAS takes one mutex acquire out of the call (about 10 ns of 96 on the i9, nothing measurable on the M4); `UUIDGenerator::operator()` still takes its own mutex for every mint, and `newUUID()` another on macOS. `test_v6_threads` covers the claim, and fails when the claim reuses a time or is not atomic.
- **`expand_mt_compute` (`cuuid_common.h`) exists for `test/test_mt.cc`**, which compares it with `std::mt19937`. Nothing else calls it.
- **Public API stays global.** `UUID`, `UUIDGenerator`, and `UUID_LENGTH` remain in the global namespace so Xapiand only needs include swaps.
- **Seams stay compile-time and no-op by default.** A standalone consumer must not need Xapiand's logger, exception hierarchy, or `Node` registry.

## Node salt

- **The node salt uses bitwise OR.** `UUID::compact_crush()` computes the compacted-UUID salt
  as `fnv_1a((local_node_hash | node))`, which spreads distinct nodes over all 128 salt values.
  Xapiand uses logical OR (`||`), which gives every non-zero node the same salt, so v1 ids
  compacted here carry different salt bits than Xapiand's. Stored condensed UUIDs decode
  either way, because the stored salt is read back as is. Don't "restore" the `||`.

## Build / test

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/cuuid_demo
./build/cuuid_benchmark
```
