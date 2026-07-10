# cuuid vs Snowflake vs UUIDv7 (and the wider ID landscape)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Analysis, with benchmark data from `benchmarks/compare.cc`

## TL;DR

`cuuid` is not an ID-generation scheme in the sense Snowflake or UUIDv7 are. It generates a plain **RFC 4122 version-1** UUID (via `uuid_generate_time`) and adds one distinctive thing on top: a **condensed, variable-length wire codec**. That codec is where all the interest, and all the argument, lives.

Measured on an M-series Mac, the honest picture is:

- **Size.** A compacted cuuid is **8 bytes** for any present-day timestamp, the same as a 64-bit Snowflake and half of UUIDv7's 16, but without Snowflake's worker-id coordination. The often-quoted 4-byte figure is real only for timestamps within about a year of cuuid's 2016 epoch; the existing `serialise_parse` benchmark hits it because it feeds near-zero timestamps.
- **Sortable? Yes.** The condensed wire puts time in the most-significant bits, so it sorts lexicographically by creation time, down to a **~1.64 ms** resolution floor. This is the answer to "can cuuid be sorted like Snowflake": on the wire, yes. In its **canonical 16-byte v1 form, no** (the classic v1 problem UUIDv6 exists to fix).
- **Cost.** The compact path costs **~850 to 950 ns to encode and to decode**, versus single-digit nanoseconds for Snowflake, UUIDv7, UUIDv6, and ULID. Nearly all of it is one `std::mt19937` **construction** inside `calculate_node()`. Swapping in a cheap mixer (splitmix64) does the identical job in **~2 ns**, a ~440x difference, but changes the wire bytes, so it is a format-v2 change, not a drop-in.

**Verdict.** cuuid earns its keep in exactly one niche: a store that keeps billions of ids as keys, wants them small and time-sortable, cannot pay for coordination, and controls both ends of the wire. That is Xapiand, where it was born. For a greenfield system in 2026 the better default is **UUIDv7** (standard, sortable, no MAC leak, libraries everywhere), or **Snowflake** if a 64-bit id and worker coordination are acceptable, or **UUIDv6** if you specifically want the v1 data model made natively sortable. The Mersenne-Twister cost is a fixable wart, not a design law.

## What each one actually is

| | cuuid (v1 + condensed) | Snowflake | UUIDv7 | UUIDv6 | ULID |
|---|---|---|---|---|---|
| Logical width | 128 bit | 64 bit | 128 bit | 128 bit | 128 bit |
| Wire size | **4 to 17 bytes** (variable) | 8 bytes | 16 bytes | 16 bytes | 16 bytes |
| Time field | 60-bit, 100 ns, rebased to 2016 | 41-bit ms, custom epoch | 48-bit ms, Unix | 60-bit, 100 ns (v1) | 48-bit ms, Unix |
| Byte-sortable (wire) | yes (~1.64 ms floor) | yes (1 ms + seq) | yes (1 ms) | yes (100 ns) | yes (1 ms) |
| Byte-sortable (canonical) | **no** (v1 layout) | n/a | yes | yes | yes |
| Coordination | none (MAC or salt) | **worker-id required** | none | none | none |
| Random bits | 0 | 0 (12-bit seq) | 74 | 0 (uses MAC/clock) | 80 |
| Standard | custom (Xapiand) | Twitter, de-facto | RFC 9562 | RFC 9562 | de-facto spec |
| Privacy | leaks MAC + time (v1) | leaks time + worker | time only | leaks MAC + time | time only |

The one row that captures cuuid's whole personality is **wire size**: it is the only scheme here whose encoded size is variable, and shrinking it is the entire reason the library's codec exists.

## Benchmark results

From `benchmarks/compare.cc` (release build, N = 200k). Numbers are ns/op; reproduce with `./build/cuuid_compare`.

### Generation and codec

| Scheme | generate | serialise | parse | wire bytes |
|---|---|---|---|---|
| cuuid v1, no compact | 91 | 823 | 9.7 | 16 |
| cuuid v1, compact | 953 | 883 | 904 | 8 |
| UUIDv7 | 4.5 | 0.7 | 0.4 | 16 |
| UUIDv6 | 0.3 | ~0 | ~0 | 16 |
| ULID | 4.7 | ~0 | ~0 | 16 |
| Snowflake | 0.7 | 1.1 | 2.2 | 8 |

Two things stand out. First, cuuid's codec is two to three orders of magnitude more expensive than the others, on both encode and decode. Second, even the **expanded** (non-compacted) serialise costs ~820 ns, because `serialise_condensed()` always calls `calculate_node()` to decide between the compact and expanded layouts, and that call is the expensive one.

### Wire size is time-dependent

The compact path strips leading zero bytes from a timestamp rebased to 2016, so its size grows as the clock moves away from that epoch:

| Year | Compact wire size |
|---|---|
| 2016 | 4 bytes |
| 2018 to 2060 | 8 bytes |
| 2100 | 9 bytes |

So the practical figure is **8 bytes**, and the headline 4-byte case only applies right at the epoch. This is the single most important correction to the intuition the old benchmark gives.

### Sortability

**E1, one strictly increasing timestamp per id** (does the format sort by creation time at all):

| Scheme | fraction ordered |
|---|---|
| cuuid condensed wire | 1.0000 |
| cuuid expanded wire | 1.0000 |
| v1 canonical 16 bytes | 0.9999 |
| UUIDv6 | 1.0000 |
| UUIDv7 | 1.0000 |
| ULID | 1.0000 |
| Snowflake | 1.0000 |

The v1 canonical form is the outlier. Its 0.9999 is not "almost perfect", it is "perfect except for a full-range backward jump every time the 32-bit `time_low` field wraps" (about every 7 minutes of real time). Those rare jumps are exactly what scatter B-tree inserts and are the reason UUIDv6 and UUIDv7 exist. cuuid sidesteps it by sorting on the **condensed wire**, not the canonical bytes.

**E2, many ids inside one millisecond** (does order survive within an instant):

| Scheme | fraction ordered |
|---|---|
| cuuid condensed wire | 0.49 |
| UUIDv6 | 1.0000 |
| UUIDv7 | 0.50 |
| ULID | 0.50 |
| Snowflake | 0.9998 |

Below its time resolution, a scheme keeps order only if it has a monotone tie-breaker. Snowflake has its sequence counter; UUIDv6 carries the full 100 ns clock. Default UUIDv7 and ULID randomize the low bits, so within a millisecond they shuffle (both specs define an optional monotone counter that fixes this). cuuid folds the low 14 bits of the timestamp into the clock field with an XOR, so below its floor it also loses order. That floor is exactly **2^14 x 100 ns = 1.64 ms**, confirmed by F:

| Step between ids | cuuid fraction ordered |
|---|---|
| 1 us | 0.50 |
| 1 ms | 0.84 |
| **1.64 ms** | **1.0000** |
| 2 ms and above | 1.0000 |

## Where it shines, where it breaks

**Best case (shines):**

- A store that holds enormous numbers of ids as keys and wants them small, sortable, and coordination-free. 8 bytes for a globally-unique, time-ordered id, without Snowflake's worker registry, is a genuinely good deal at scale.
- Time-clustered or near-epoch data, where the variable-length encoding can dip below 8 bytes.
- A closed system that owns both ends of the wire, so the custom format costs nothing in interop.
- Correctness holds everywhere: every round-trip in the test suite is byte-exact, in both compact and expanded modes.

**Worst case (breaks down, though never incorrect):**

- **Read-heavy workloads.** Every decode of a compacted id runs a Mersenne-Twister construction (~880 ns). At millions of reads per second that is real CPU and real money.
- **Sub-millisecond ordering.** Anything needing strict order at finer than ~1.64 ms will not get it from the condensed wire.
- **Non-v1 UUIDs.** A v4 (random) UUID has no compressible structure, so cuuid falls back to the full 17-byte form (tag + 16 bytes), which is **larger** than a raw 16-byte UUID. The compression is a v1-only trick.
- **Canonical-form sortability and privacy.** If a consumer sorts or indexes the canonical 16 bytes rather than the wire, it gets v1's non-sortability and its MAC-address leak.

Nothing here is a correctness bug. These are the honest edges of a design that traded CPU and a custom format for bytes on disk.

## Two ways forward

### 1. Kill the Mersenne Twister (a format v2)

`calculate_node()` reconstructs a synthetic 48-bit node from `(time, clock, salt)`. The node's actual value is arbitrary; it just has to be deterministic, carry the salt, and set the multicast bit. The current code uses `std::mt19937`, whose per-call **construction** (a 624-word state init) is the ~880 ns. A splitmix64 mixer produces an equally good synthetic node in **~2 ns** (section G), which would drop the compact path from ~1 us to roughly the cost of the `std::string` allocation and bit-packing, tens of nanoseconds.

The catch is that the node bytes are part of the wire format, so changing the mixer changes what a compacted id decodes to. That makes it a **breaking, versioned change**, gated behind a new tag so existing `0x01` and current condensed ids still decode. Worth doing precisely because it removes the library's one embarrassing number without touching what it is good at.

### 2. Store as UUIDv6 for native canonical sortability

cuuid's canonical form is v1, which is why it needs the condensed-wire trick to be sortable at all. **UUIDv6** is the same v1 data (Gregorian time, clock, node) with the timestamp reordered most-significant-first, so the **canonical 16 bytes sort by time on their own**. Migrating cuuid's data model to v6 would mean a consumer that indexes the raw bytes gets ordering for free, no codec required.

This is orthogonal to size: v6 is still 16 bytes canonical. The appealing combination is a **v2 that is v6 underneath, uses splitmix64 for the synthetic node, and keeps the variable-length size trick**, giving native canonical sortability, cheap encode/decode, and an 8-byte compact wire at once. Compatibility is keepable by tagging: old ids decode by their existing markers, new ids by the v2 marker. Not required, but it is the version this analysis would build if starting over.

## The wider landscape (prior art)

cuuid sits in a crowded field of "make an id that is unique, roughly time-ordered, and cheap to index". The recurring tension is the same everywhere: **coordination vs. randomness vs. size**, and how the timestamp is laid out for sorting.

- **UUIDv1 / v4** (RFC 4122): the originals. v1 is time + clock + MAC (sortable only after field reordering, leaks the MAC); v4 is 122 random bits (never sortable). cuuid is a v1 with a codec bolted on.
- **UUIDv6 / v7 / v8** (RFC 9562, 2024): the modern answer. v6 reorders v1 to be sortable; v7 is ms-timestamp + randomness, the recommended default; v8 is a free-form vendor layout.
- **ULID**: 48-bit ms + 80-bit randomness, Crockford base32 text, sortable. The popular pre-standard ancestor of v7.
- **Snowflake** (Twitter) and its kin, **Sonyflake**, **Instagram's** scheme, **Boundary's Flake**, **Discord/Mastodon** ids: 64-bit, timestamp + machine + sequence. Small and sortable, at the price of a coordinated machine/worker id.
- **MongoDB ObjectId**: 12 bytes, 4-byte seconds + 5-byte random + 3-byte counter. Sortable to one-second granularity.
- **KSUID**: 20 bytes, 4-byte seconds (custom epoch) + 16 random. Sortable, generous randomness, no coordination.
- **xid**: 12 bytes, MongoDB-like, base32 text.
- **TSID / Crockford-style Snowflakes**: 64-bit Snowflake ideas packaged as sortable ids with nicer text encodings.
- **NanoID, CUID2**: deliberately **not** time-sortable; they optimize for URL-friendly, collision-resistant, unguessable strings. The opposite end of the trade from cuuid.
- **Prefixed ids** (Stripe's `cus_...`, and similar): a type tag plus a random or sortable body. A product convention, orthogonal to the encoding underneath.

Seen against all of them, cuuid's one genuinely unusual property is **variable-length binary size**. Every other scheme here is fixed-width. That is the thing to keep, and the Mersenne Twister is the thing to drop.

## Reproducing

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cuuid_compare
./build/cuuid_compare
```

UUIDv6, UUIDv7, ULID, and Snowflake are implemented inline in `benchmarks/compare.cc` so the harness is self-contained; cuuid is the real library. Absolute ns/op will vary by machine; the ratios are the point.
