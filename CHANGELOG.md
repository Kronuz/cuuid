# Changelog

Notable changes to `cuuid`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## 2026-11-01

### Added

- `cuuid_compare` adds section H, built with `-DCUUID_BENCH_BOOST=ON` (Boost is found, never
  fetched): the 36-character text form and generation against Boost.UUID 1.92 on an Apple M4 Pro
  and an Intel i9-9980HK. It checks that both libraries agree on the text form first. cuuid
  parses the text 9x (M4 Pro) and 7x (i9) faster, prints it 1.2x and 1.4x slower, and
  generates v6 ids 2.6x to 6.6x slower than Boost's `time_generator_v7` and `random_generator`.
  The README has the table.

## 2026-10-31

### Changed

- The v6 `generate()` claim stays a CAS, and the "lock-free" description is
  corrected. `generate()` is not lock-free: `UUIDGenerator::operator()` takes a
  static mutex for every mint, and `UUIDGenerator::newUUID()` takes another on
  macOS, before the claim runs. What the CAS removes is the claim's own mutex
  acquire, about 10 ns per call on x86. Measured with alternating runs of
  `cuuid_v1_bench` and a threaded loop (median of rounds; three on the M4, five
  on the i9), the mutex against the CAS:

  | Row | Apple M4 Pro | Intel i9-9980HK |
  | --- | ---: | ---: |
  | `v6 generate`, `cuuid_v1_bench` | 90.9 and 90.9 ns | 109.6 and 99.9 ns |
  | `v6 generate`, 1 thread | 90.9 and 90.9 ns | 107.1 and 97.7 ns |
  | per thread, 2 threads | 239 and 214 ns | 525 and 484 ns |
  | per thread, 4 threads | 633 and 715 ns | 1307 and 1199 ns |
  | per thread, 8 threads | 1566 and 1496 ns | 3412 and 3315 ns |
  | `v1 generate (no compact)`, unchanged code | 90.9 and 90.9 ns | 90.9 and 90.9 ns |

  On the M4 the platform mint sets the floor and the two are the same; the threaded rows
  move in both directions with spreads up to 8%. On the i9 the mutex is 8 to 10% slower
  with one to four threads and the 8-thread rows are inside their 15 to 30% spread. The
  CAS is kept for that.

### Added

- `test/test_v6_threads.cc` (`cuuid_v6_threads`): 8 threads each mint 4,000 v6 ids
  with `generate()`. Every id and every condensed wire is distinct, each id round-trips,
  and the claimed 100 ns time of every id (recovered by undoing the compaction)
  is unique across threads and strictly increasing within each thread. It passes
  under ThreadSanitizer. Mutants: a claim that reuses the last time and a claim that
  stores instead of compare-exchanging each fail it in 6 runs of 6; neither failed any
  earlier test. A claim step that is removed altogether passes, because
  `UUIDGenerator::operator()` already hands out unique, strictly increasing times; the
  step is redundant on platforms whose mint is a version 1 UUID.
- `test_v6_canonical_order` in `test/test_v6.cc`: the canonical v6 bytes of `make()` ids are in time order
  across 1.64 ms windows, and distinct inside them. A consistent swap of two time
  bytes in the layout, which keeps the wire sorted, fails this test and none of the earlier ones.

### Documentation

- The canonical v6 bytes sort by time at a granularity of 2^14 x 100 ns = 1.64 ms,
  not 1 ms. `make()` and `generate()` compact the id, which clears the low 14 bits of
  the time and folds them into the clock field with an XOR of the clock sequence, so
  ids from different windows are in time order and ids inside one window are
  distinct but not in creation order. For 100,000 ids from one thread, 33% to 58%
  of consecutive ids sort below their predecessor on Linux over three runs
  (55% and 97% in the audit; the rate depends on the process clock sequence).
  `docs/CUUID_V6.md` described the prototype's 1 ms, no-fold layout as the
  shipped one; it and the `cuuid_v6` comments now state the shipped granularity. A
  finer canonical order needs the exact time in the id, which the compact wire
  cannot carry (it keeps the low 14 bits of the time only XORed into the clock), so
  such ids would serialise in the expanded form, up to 17 bytes instead of about 8:
  a design decision.

- The timings in `README.md`, `docs/COMPARISON.md` and `docs/CUUID_V6.md` are measured
  on an Apple M4 Pro (Apple clang 17, `-O3`) at `d03d8d1`, after the v1 node
  expander below. The figures they replace were measured on an unrecorded M-series Mac
  with the full `std::mt19937` engine:

  | Figure | Before | Now |
  | --- | --- | --- |
  | v1 compact `generate` / `serialise` / `parse` | 953 / 883 / 904 ns | 682 / 621 / 624 ns |
  | v1 no-compact `generate` / `serialise` / `parse` | 91 / 823 / 9.7 ns | 91 / 621 / 11 ns |
  | UUIDv7 `generate` / `serialise` / `parse` | 4.5 / 0.7 / 0.4 ns | 4.9 / 0.8 / 0.4 ns |
  | UUIDv6, ULID `generate` | 0.3, 4.7 ns | 1.1, 5.0 ns |
  | Snowflake `generate` / `serialise` / `parse` | 0.7 / 1.1 / 2.2 ns | 0.7 / 1.0 / 2.8 ns |
  | v1 encode / decode in the `cuuid_v2_demo` table | 880 / 850 ns | 620 / 620 ns |
  | v6 encode / decode | 9 / 12 ns (7 and 16 ns decode in two places) | 11 / 12 ns |
  | v2 prototype encode / decode | 12 / 8 ns | 9 / 5 ns |
  | v1 decode in `docs/CUUID_V6.md` | 843 to 1043 ns | 620 ns |
  | v6 decode speedup over v1 | 70 to 120x | about 50x |

  The v6 `serialise` and `unserialise` figures come from a scratch loop over 4,096
  distinct ids with `Codec::v6`; `cuuid_v1_bench` times only v1 and v6 `generate`.

## 2026-10-30

### Changed

- The v1 compact node is the first two `std::mt19937(seed)` outputs.
  `expand_mt_compute` runs the init recurrence up to word 398 and twists only the
  words those two outputs read, instead of constructing the engine (624 words)
  and twisting all of them. A small thread-local direct-mapped cache (8 entries,
  keyed by seed) covers `generate()` followed by `serialise()` of the same id.
  Wire bytes are unchanged.
- v6 `generate()` keeps its monotonic time with a lock-free CAS on an atomic
  instead of a mutex. It yields the same sequence.
- Speed of v1 `serialise`, on an Apple M4 Pro, from `cuuid_v1_bench`:

  | Case | Before | After |
  | --- | --- | --- |
  | New id | 900 ns | 590 ns |
  | Repeated id | 911 ns | 9 ns |

  The v1 figures in `docs/COMPARISON.md` and `docs/CUUID_V6.md` (about 850 to
  1043 ns) were measured with the full engine.

### Added

- `test/test_mt.cc` compares the expander with the real engine over many seeds,
  and the wire bytes with a frozen full-engine expander through
  condense/uncondense.
- `benchmarks/v1_bench.cc` (`cuuid_v1_bench`).

## 2026-10-28

### Changed

- **Breaking:** the node salt uses bitwise OR. `compact_crush()` computes the
  salt as `fnv_1a((local_node_hash | node))`. The Xapiand code uses logical OR
  (`||`), which collapses the node to a boolean before hashing, so every
  non-zero node produced the same salt (`-Wconstant-logical-operand` flags it).
  Over 200k distinct nodes the `||` form yields 1 distinct salt; `|` yields the
  full 128-value spread. Newly compacted v1 ids carry different salt bits than
  before; previously stored condensed ids decode unchanged, because the stored
  salt is read back as is.
