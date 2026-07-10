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

## Prototype: a working v2

I built that v2 as a standalone prototype (`prototype/cuuid_v2.hh` and `prototype/v2_demo.cc`) to check the design end to end. It does not touch the frozen `uuid.hh` / `uuid.cc`; it stores as UUIDv6, reconstructs the compacted node with splitmix64, stores the timestamp as milliseconds since a 2026 epoch, keeps the variable-length wire, and carries a distinct tag byte (`0x00`) so old readers reject it and old ids never look like v2 (see "Backward compatibility" below). Measured (`./build/cuuid_v2_demo`, same machine):

| | v2 prototype | v1 (current) |
|---|---|---|
| encode | ~12 ns | ~880 ns |
| decode | ~8 ns | ~850 ns |
| compact payload | 4 bytes (2026) to 8 bytes (steady) | 8 bytes |
| compact wire total | payload + 2 framing | 8 bytes |
| condensed wire sortable | yes (1.0000) | yes (1.0000) |
| **canonical bytes sortable** | **yes (1.0000, v6)** | **no (v1)** |

- **Correctness.** 200k random ids round-trip with zero failures in both compact and expanded modes, and through the canonical v6 form.
- **Speed.** The Mersenne Twister is gone; encode and decode drop by ~70x and ~100x. This is the whole-codec number, not just the mixer in isolation.
- **Sortability.** Both the condensed wire and the raw v6 bytes sort by creation time. The second one is the real gain: a consumer can index the canonical bytes directly, no codec on the read path at all.
- **Coexistence.** Old and new ids live in one store, routed by the first byte (see below). Old data keeps decoding.

### Backward compatibility: feeding old v1 ids to v2

The important question for any format change: what happens to the ids already on disk? Xapiand stores them as the `~`-prefixed text form of these exact wire bytes, so a v2 reader will be handed old v1 wires constantly, and it must never silently misread one.

The tag choice is what makes this safe, and it is not free. The obvious pick, `0x02`, is a trap: `0x02` is a real entry in v1's length-prefix table (the length-11 code), so a v1 condensed wire genuinely can begin with it, and a dispatcher would then route an old id into the v2 decoder and hand back garbage. The safe pick is **`0x00`**, and it is safe by proof, not by sampling.

A one-byte tag lives in a space of only 256 values, so "no v1 wire can start with this byte" is a claim you can settle **exhaustively**, not estimate. Enumerating all 256 against v1's exact decode rules:

```text
v1 uses 255 of 256 possible first bytes.
bytes NO v1 wire can start with: 0x00
=> 0x00 is the unique safe v2 tag. (0x02 is valid, i.e. UNSAFE.)
```

`0x00` is the *only* byte no v1 wire can begin with, and the reason is structural, not statistical: a full v1 wire is the hard-coded byte `0x01`, and a condensed wire strips its leading zero bytes and then OR-s in a nonzero length prefix, so its first byte is always nonzero. The real library agrees: feeding it 256 buffers that start with `0x00` (varied trailing bytes) rejects all 256. This is also why a random sample, even a large one, cannot *ensure* the property: v1 can emit 255 distinct first bytes, so no finite sample covers the space; the proof does. The 720,000-id probe below is a cross-check, not the argument.

With `0x00` as the tag, the prototype confirms the end-to-end behavior against the real library. It generates 720,000 v1 wires (compact and expanded, times spanning decades so every encoded length occurs) and feeds each to the v2 decoder and to a first-byte dispatcher:

```text
V2_TAG=0x00 appears as a v1 first byte: compact=no expanded=no
v1 wires the v2 decoder wrongly accepted: 0 [OK, all rejected]
v1 wires the dispatcher routed correctly to v1: 720000 / 720000 [OK]
```

So the answer is: a v2 decoder handed an old v1 id **rejects it cleanly** (throws, never a silent misread), and the real deployment is a five-line dispatcher, "first byte `0x00` means v2, `0x01` means v1-full, anything else means v1-condensed", which routes every id to the right decoder. It also works the other way: an old v1 reader handed a v2 wire sees a `0x00` first byte, finds no matching condensed prefix, and throws, so old code refuses new ids instead of mangling them. Both directions fail loud, which is exactly what you want from a format bump. (Reproduce with `./build/cuuid_v2_demo --compat` and `python3 prototype/analysis.py`.)

### The time encoding

The compact wire spends most of its bytes on the timestamp, so that is where size is won or lost. Two changes, both essentially free:

- **Rebase the epoch to 2026.** The delta from the epoch is what gets stored, so a 2026 epoch makes present-day ids tiny: a compact id minted in 2026 has a **4-byte** payload, versus v1's 8. The delta grows over time (about 35 bits per year in ms), so the payload climbs back to 8 bytes within a year or two, but the near-term win is real and the epoch can be re-based again at the next format revision.
- **Store milliseconds, not 100ns ticks.** v1 carries a 60-bit Gregorian 100ns timestamp and then folds away its low 14 bits with an XOR, which is really a clumsy way of quantizing to ~1.64ms. Storing plain milliseconds gets the same resolution (1ms) without the fold, which is why the v2 codec has no XOR and no clock-scrambling: cleaner to read, cleaner to port, and it gives a proper 1ms sort granularity with the clock as a tie-breaker instead of v1's scrambled 1.64ms floor. Honestly, v1's fold already approximated milliseconds, so the steady-state payload is the same 8 bytes; the ms form's gain is that it stays flat at 8 out to 2100 (v1 creeps to 9) and, mostly, that it is far simpler.

The canonical form stays a full RFC-valid UUIDv6 (the sub-millisecond digits are just zero), so it still sorts at millisecond resolution.

### The ports, and why they get simpler

The wire format is a **three-language contract**. Xapiand ships reference implementations in [Python](https://github.com/Kronuz/Xapiand/tree/master/contrib/python/cuuid) and [JavaScript](https://github.com/Kronuz/Xapiand/tree/master/contrib/javascript/cuuid) alongside the C++, and the Python port literally includes `mertwis.py`, a hand-rolled Mersenne Twister. It exists for one reason: `std::mt19937` had to be reproduced bit-for-bit in every language or the reconstructed node bytes would not match across systems. That is a nasty thing to depend on.

splitmix64 removes the problem at the root. It is pure fixed-width integer arithmetic (add, xor, shift, multiply, all mod 2^64), with no floating point and no platform RNG, so it produces identical bytes everywhere by construction. To prove it, the prototype ships the two ports (`prototype/python/cuuid_v2.py`, `prototype/javascript/cuuid_v2.mjs`), each about 40 lines, and a cross-check (`prototype/cross_validate.sh`): the C++ codec emits test vectors and both ports reproduce every wire **byte-for-byte**.

```text
C++ emitted 36 test vectors
Python port: 36 vectors match C++ byte-for-byte [OK]
JavaScript port: 36 vectors match C++ byte-for-byte [OK]
cross-language: C++ == Python == JavaScript [OK]
```

So a v2 is three coordinated changes, not one, but it is a **simplifying** three: every port deletes its Mersenne Twister (the whole of `mertwis.py`) and replaces it with a four-line splitmix64, and the cross-system determinism stops being something you pray for and becomes something the arithmetic guarantees. That trade, three small coordinated ports against a permanently cheaper, natively-sortable, and easier-to-port format, is the actual decision, and it is a good one whenever the format is next revised.

### Collisions and the millisecond question

Storing the timestamp as milliseconds sounds coarser than v1's 100ns, so the fair worry is collisions. But the comparison is not against a full 100ns UUID, it is against the **compact form we actually used**, and that was never 100ns: v1's compact path folds the low 14 bits of the 100ns timestamp away, which quantizes it to `2^14 * 100ns = 1.6384 ms`. Against that real baseline, v2's 1ms is **finer**, not coarser.

A compacted id on one node is unique per `(time bucket, clock_seq)` (the salt is `node & 0x7f`, fixed per node). So what matters is how many ids fall in one time bucket, and smaller buckets hold fewer. Before the 14-bit clock sequence is exhausted:

| | time bucket | max ids/sec/node |
|---|---|---|
| full v1 (100ns, uncompacted) | 0.0001 ms | ~164 billion |
| v1 compact (1.6384 ms fold) | 1.6384 ms | ~10 million |
| **v2 compact (1 ms)** | 1.0 ms | **~16.4 million** |

And if the clock sequence were random rather than a counter (the worst case), the birthday collision risk per node at 100k ids/sec is **~26% for v2 vs ~56% for v1 compact**. Either way v2 is the safer of the two compact forms. Cross-node uniqueness is unchanged: expanded ids still carry the full 48-bit node, and compact ids reduce it to the same 7-bit salt in both v1 and v2. So the move to milliseconds does not cost collision resistance against the form it replaces; it slightly improves it.

### The compression idea, and the bigger win

A tempting idea is to spend 2 or 3 header bits on a "compression" scheme, a floating epoch or an exponent that keeps the encoded timestamp short as the years pass. It does not pay off, for a concrete reason: a compact id is `time + clock(14) + salt(7) + flag(1)`, and that **21-bit clock-plus-salt floor is not compressible** (it is pseudo-random and load-bearing for collisions). A present-day timestamp needs about 35 bits per year at millisecond resolution, so a current id is already near `(35 + 21 + 1)/8 ~= 7 bytes`, and no exponent trick shrinks the part that dominates. An exponent only helps the far-future tail, and that tail grows logarithmically: a fixed 2026 ms epoch stays at an 8-byte payload until 2166 and only then ticks to 9. Spending per-id bits, in three languages, to save under a byte a century and a half out is a bad trade. If the epoch ever does need moving, the next format version moves it, for free.

The compression worth having is the one **sortability already unlocks**. v2 ids sort by time, so in any sorted store (an LSM SSTable, a B-tree with prefix truncation, RocksDB) consecutive ids share their high-order bytes and the store keeps only the difference. Front-coding a sorted run of v2 compact ids measures like this:

| generation rate | raw wire | front-coded | saved |
|---|---|---|---|
| 10 /s | 9.00 B | 5.10 B | 43% |
| 1,000 /s | 9.00 B | 4.25 B | 53% |
| 100,000 /s | 9.00 B | 3.51 B | 61% |

The id drops to **3.5 to 5 bytes** stored, and it compresses *better* the faster you mint them, because more ids cluster into each shared time prefix. That is a general, standard, free win, and it is exactly the payoff of having made the ids sortable in the first place. The 2-3 bit scheme tries to hand-compress one id in isolation; the sorted store compresses the whole run for you, and does it better.

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
cmake --build build --target cuuid_compare cuuid_v2_demo
./build/cuuid_compare    # the comparison across schemes
./build/cuuid_v2_demo    # the working v2 prototype
./prototype/cross_validate.sh   # C++ == Python == JavaScript, byte-for-byte
```

UUIDv6, UUIDv7, ULID, and Snowflake are implemented inline in `benchmarks/compare.cc` so the harness is self-contained; cuuid is the real library. The v2 prototype lives in `prototype/` (C++ codec, Python and JavaScript ports, and the cross-language check) and does not touch the frozen codec. Absolute ns/op will vary by machine; the ratios are the point.
