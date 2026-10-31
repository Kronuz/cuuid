# Changelog

Notable changes to `cuuid`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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
