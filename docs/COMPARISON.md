# cuuid vs Snowflake vs UUIDv7 (and the wider ID landscape)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Analysis, with benchmark data from `benchmarks/compare.cc`

## TL;DR

`cuuid` is not an ID-generation scheme in the sense Snowflake or UUIDv7 are. It generates a plain **RFC 4122 version-1** UUID (via `uuid_generate_time`) and adds one distinctive thing on top: a **condensed, variable-length wire codec**. That codec is where all the interest, and all the argument, lives.

Measured on an M-series Mac, the honest picture is:

- **Size.** A compacted cuuid is **8 bytes** for any present-day timestamp, the same as a 64-bit Snowflake and half of UUIDv7's 16, but without Snowflake's worker-id coordination. The often-quoted 4-byte figure is real only for timestamps within about a year of cuuid's 2016 epoch; the existing `serialise_parse` benchmark hits it because it feeds near-zero timestamps.
- **Sortable? Yes.** The condensed wire puts time in the most-significant bits, so it sorts lexicographically by creation time, down to a **~1.64 ms** resolution floor. This is the answer to "can cuuid be sorted like Snowflake": on the wire, yes. In its **canonical 16-byte v1 form, no** (the classic v1 problem UUIDv6 exists to fix), which is exactly what the shipped **v6** format cures by storing the bytes in UUIDv6 order.
- **Cost.** The compact path costs **~850 to 950 ns to encode and to decode**, versus single-digit nanoseconds for Snowflake, UUIDv7, UUIDv6, and ULID. Nearly all of it is one `std::mt19937` **construction** inside `calculate_node()`. Swapping in a cheap mixer (splitmix64) does the identical job in **~2 ns**. This is now **shipped as the v6 format** (see [Two ways forward](#two-ways-forward)): reconstructing the node with splitmix64, plus byteswapping the condenser's two 64-bit words (a single `bswap` instead of a byte loop), drops the whole compact path to a measured **~9 ns to encode and ~12 ns to decode** (release, N = 1M), roughly 70 to 90x faster. It changes the reconstructed node bytes, so it is a new format, told apart from v1 by the standard UUID version nibble rather than a drop-in patch.

**Verdict.** cuuid earns its keep in exactly one niche: a store that keeps billions of ids as keys, wants them small and time-sortable, cannot pay for coordination, and controls both ends of the wire. That is Xapiand, where it was born. For a greenfield system in 2026 the better default is **UUIDv7** (standard, sortable, no MAC leak, libraries everywhere), or **Snowflake** if a 64-bit id and worker coordination are acceptable, or **UUIDv6** if you specifically want the v1 data model made natively sortable. The Mersenne-Twister cost was a fixable wart, not a design law, and the **v6** format fixes it while keeping the 8-byte wire.

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

- **Read-heavy workloads.** Every decode of a compacted **v1** id runs a Mersenne-Twister construction (~880 ns). At millions of reads per second that is real CPU and real money. (The **v6** format removes this: splitmix64 reconstruction decodes in ~16 ns.)
- **Sub-millisecond ordering.** Anything needing strict order at finer than ~1.64 ms will not get it from the condensed wire.
- **Non-v1 UUIDs.** A v4 (random) UUID has no compressible structure, so cuuid falls back to the full 17-byte form (tag + 16 bytes), which is **larger** than a raw 16-byte UUID. The compression is a v1/v6-only trick.
- **Canonical-form sortability and privacy.** If a consumer sorts or indexes the canonical 16 bytes rather than the wire, **v1** gives non-sortability and a MAC-address leak. (The **v6** format fixes the sortability: its canonical bytes are UUIDv6, time-ordered; the node is still a synthetic, salted value, not a MAC.)

Nothing here is a correctness bug. These are the honest edges of a design that traded CPU and a custom format for bytes on disk.

## Two ways forward

> **Update: both of these shipped, combined, as the `v6` format.** It lives beside v1
> (`cuuid_v6.{hh,cc}` + the shared `cuuid_common`), is told apart on encode by the UUID
> **version nibble** (1 vs 6), and reuses v1's condenser, time base, and salt derivation
> verbatim, swapping only `std::mt19937` for splitmix64 and laying the bytes out in UUIDv6
> order. That makes it a smaller delta than the from-scratch prototype below (which explored
> a separate ms-since-2026 time base and its own splitmix derivation). Measured on the shipped
> library: **~9 ns encode, ~12 ns decode**, 8-byte compact wire, canonical form sorts by time.
> Xapiand mints v6 by default; `--legacy-ids` pins pure v1.

### 1. Kill the Mersenne Twister (a format v2)

`calculate_node()` reconstructs a synthetic 48-bit node from `(time, clock, salt)`. The node's actual value is arbitrary; it just has to be deterministic, carry the salt, and set the multicast bit. The current code uses `std::mt19937`, whose per-call **construction** (a 624-word state init) is the ~880 ns. A splitmix64 mixer produces an equally good synthetic node in **~2 ns** (section G), which would drop the compact path from ~1 us to roughly the cost of the `std::string` allocation and bit-packing, tens of nanoseconds.

The catch is that the node bytes are part of the wire format, so changing the mixer changes what a compacted id decodes to. That makes it a **breaking, versioned change**, gated behind a new tag so existing `0x01` and current condensed ids still decode. Worth doing precisely because it removes the library's one embarrassing number without touching what it is good at.

### 2. Store as UUIDv6 for native canonical sortability

cuuid's canonical form is v1, which is why it needs the condensed-wire trick to be sortable at all. **UUIDv6** is the same v1 data (Gregorian time, clock, node) with the timestamp reordered most-significant-first, so the **canonical 16 bytes sort by time on their own**. Migrating cuuid's data model to v6 would mean a consumer that indexes the raw bytes gets ordering for free, no codec required.

This is orthogonal to size: v6 is still 16 bytes canonical. The appealing combination is a **v2 that is v6 underneath, uses splitmix64 for the synthetic node, and keeps the variable-length size trick**, giving native canonical sortability, cheap encode/decode, and an 8-byte compact wire at once. Compatibility is keepable by tagging: old ids decode by their existing markers, new ids by the v2 marker. Not required, but it is the version this analysis would build if starting over.

## Prototype: a working v2

I built that v2 as a standalone prototype (`prototype/cuuid_v2.hh` and `prototype/v2_demo.cc`) to check the design end to end. It does not touch the frozen `uuid.hh` / `uuid.cc`; it stores as UUIDv6, reconstructs the compacted node with splitmix64, stores the timestamp as milliseconds since a 2026 epoch, and keeps the variable-length wire with a nonzero first byte so the `~` text form survives (see "Backward compatibility and the text form" below). Measured (`./build/cuuid_v2_demo`, same machine):

| | v2 prototype | v1 (current) |
|---|---|---|
| encode | ~12 ns | ~880 ns |
| decode | ~8 ns | ~850 ns |
| compact wire | 5 bytes (2026) to 9 bytes steady (8 with a VL-folded length, = v1) | 8 bytes |
| condensed wire sortable | yes (1.0000) | yes (1.0000) |
| **canonical bytes sortable** | **yes (1.0000, v6)** | **no (v1)** |

- **Correctness.** 200k random ids round-trip with zero failures in both compact and expanded modes, and through the canonical v6 form.
- **Speed.** The Mersenne Twister is gone; encode and decode drop by ~70x and ~100x. This is the whole-codec number, not just the mixer in isolation.
- **Sortability.** Both the condensed wire and the raw v6 bytes sort by creation time. The second one is the real gain: a consumer can index the canonical bytes directly, no codec on the read path at all.
- **Coexistence.** Old and new ids live in one store, versioned out of band (see below). Old data keeps decoding.

### Backward compatibility and the text form

Two things must hold when a format changes: the ids already on disk must keep decoding, and the new ids must survive Xapiand's `~` text encoding. That text form turns out to constrain the wire more than the binary side does, and it is worth walking through because it overturns an obvious-looking design.

Xapiand renders a serialised id as `~` followed by the **base59** of the wire bytes (`ENCODER = base_x.b59`). base59 folds the bytes into one big integer, so a **leading zero byte contributes nothing and cannot be recovered**. That single fact drives everything: the wire's first byte must be nonzero.

Now, the tempting way to version a new format is a leading tag byte, and there is exactly one byte that no v1 wire can start with. The tag space is only 256 values, so this is settled exhaustively, not sampled:

```text
v1 uses 255 of 256 possible first bytes.
bytes NO v1 wire can start with: 0x00
```

`0x00` is the unique free byte, for a structural reason: a full v1 wire is the hard-coded `0x01`, and a condensed wire strips its leading zeros and then OR-s in a nonzero prefix, so its first byte is always nonzero. So the only byte available for a leading tag is `0x00`, and `0x00` is precisely the byte base59 destroys. The two constraints collide, and the conclusion is clean: **there is no usable leading version byte at all.** That is not a limitation of v2; it is why v1 itself has no version byte and carries its compact/expanded flags in the trailing bytes.

So v2 does the same. No leading tag: the wire is `[length][payload]`, the first byte is the nonzero length, and v1-versus-v2 is decided **out of band** (an index/schema version) rather than by staring at a byte. The prototype confirms the wire is text-safe and that a leading tag would not have been:

```text
v1 distinct first bytes: 73   v2 distinct first bytes: 5   overlap: 3
v2 wires that start with 0x00 (base59-unsafe): 0 [OK, all nonzero]
```

Concretely, here is one id in every form (from `python3 prototype/strings_demo.py`):

```text
fields         time=140029344000000000 clock=6844 node=0x0ff76d02512b
canonical v6   1f17bf24-b404-6000-9abc-0ff76d02512b
binary wire    07f49e12001abc57   (8 bytes: 0x07 length + 7 payload)
'~' base59     ~GEDryoPFbV6N
```

The first byte `0x07` is the length; it is nonzero, so `~GEDryoPFbV6N` round-trips back to the exact wire. Prepending a `0x00` tag would encode to the same base59 string as the tag-less wire, and decode back without the tag, silently, which is the whole reason the tag idea fails. (Reproduce with `./build/cuuid_v2_demo --compat` and `python3 prototype/strings_demo.py`.)

### The time encoding

The compact wire spends most of its bytes on the timestamp, so that is where size is won or lost. Two changes, both essentially free:

- **Rebase the epoch to 2026.** The delta from the epoch is what gets stored, so a 2026 epoch makes present-day ids tiny: a compact id minted in 2026 has a **4-byte payload** (a 5-byte wire with the length byte), versus v1's 8. The delta grows over time (about 35 bits per year in ms), so the payload climbs back up within the first day, but the near-term win is real and the epoch can be re-based again at the next format revision.
- **Store milliseconds, not 100ns ticks.** v1 carries a 60-bit Gregorian 100ns timestamp and then folds away its low 14 bits with an XOR, which is really a clumsy way of quantizing to ~1.64ms. Storing plain milliseconds gets the same resolution (1ms) without the fold, which is why the v2 codec has no XOR and no clock-scrambling: cleaner to read, cleaner to port, and it gives a proper 1ms sort granularity with the clock as a tie-breaker instead of v1's scrambled 1.64ms floor. Honestly, v1's fold already approximated milliseconds, so the steady-state payload is the same 8 bytes; the ms form's gain is that it stays flat out to ~2165 (v1's payload creeps to 9 sooner) and, mostly, that it is far simpler. See the size schedule below for the exact steps.

The canonical form stays a full RFC-valid UUIDv6 (the sub-millisecond digits are just zero), so it still sorts at millisecond resolution.

### The size schedule (when the wire grows a byte)

Because the wire is variable-length, its size is a step function of the timestamp. The steps are front-loaded (ms resolution gains a bit every doubling of elapsed time) and then very sparse. The total wire (a length byte plus payload) first reaches each size at:

| total bytes | first reached |
|---|---|
| 5 | 2026-01-01, +4 ms |
| 6 | 2026-01-01, +1 s |
| 7 | 2026-01-01, +4.4 min |
| 8 | 2026-01-01, +18.6 h |
| **9** | **2026-07-18** |
| 10 | ~2165 |
| 11 | ~year 37,700 |
| 12 | ~year 9.4 million |

So it climbs from 5 to 8 bytes within the first day of 2026, hits **9 bytes on 2026-07-18**, and then holds 9 bytes for about 139 years (to ~2165), with every later jump roughly 256x rarer than the one before. Two honest notes this makes precise: the "4 bytes near the epoch" figure is a **payload** of 4 (total 5, counting the length byte); and the prototype's steady state is **9 total bytes**, one more than v1's 8. That extra byte is the explicit length byte, which v1 avoids by folding the length into the payload's top bits (its VL table). Reclaim it the same way and v2 matches v1 at 8 bytes exactly, with all of v2's other wins intact. (Reproduce with `python3 prototype/analysis.py`.)

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

### v1 and v2, the same input in both forms

Running the same `(time, clock, node)` through the real v1 library and through v2 (`./build/cuuid_v2_demo --v1-vectors | python3 prototype/compare_v1.py`) makes the differences concrete:

```text
input  greg=140029344000000000 clock=6844 node=0x0123456789ab
  v1  canonical 4b400800-7bf2-11f1-a2bc-85d50201962b
      wire      4bcbb8833c22bc57   (8 bytes)   ~DpjALhPwn2j2
  v2  canonical 1f17bf24-b404-6000-9abc-0ff76d02512b   (UUIDv6)
      wire      07f49e12001abc57   (8 bytes)   ~GEDryoPFbV6N
```

Same size on the wire (8 bytes at this mid-2026 date), same `~` base59 shape. The differences are the ones that matter: the v1 canonical is a **version-1** UUID (note the `11f1`, and the timestamp is split and shuffled across the string, so the raw bytes do not sort by time), while the v2 canonical is a **version-6** UUID (`6000`, timestamp first, so the bytes sort). And the v2 wire decodes without a Mersenne Twister and shrinks toward a 4-byte payload near its 2026 epoch. Feed two ids a millisecond apart and the v2 forms share a long prefix (`1f17bf24-b40...`, `07f49e120...`) while the v1 forms diverge earlier, which is the sortability and prefix-compression difference showing up in the actual bytes.

### Ensuring uniqueness: how collisions are prevented

An id is unique by `(timestamp, clock_seq, node)`, and there are three independent lines of defense. The base guarantee comes from the generator (`uuid_generate_time`): a globally-unique node, a monotonic timestamp, and a `clock_seq` that is a counter, incremented within a single tick and bumped if the clock ever runs backwards.

1. **Within one node.** `clock_seq` is a 14-bit counter, so a node can mint up to **16,384 distinct ids per time bucket** before it wraps, and the timestamp advances between buckets. Below that rate it is a hard guarantee, not a probability.
2. **Across nodes, expanded form.** The full **48-bit node** is on the wire, so two machines collide only via a 48-bit birthday (about 19.7 million nodes for a 50% chance, independent of rate). This is the RFC-strength guarantee.
3. **Across nodes, compact form.** This is where compaction spends uniqueness for size: the node is reduced to a **7-bit salt** (128 values), derived like v1 as `xor_fold(fnv_1a(node), 7)` so the 128 buckets fill uniformly no matter how node values cluster (on a stride-2 node set, low-7-bits fills only 64/128 buckets; the hash fills all 128). Fleet-wide, the distinct compact ids in one millisecond are `2^7 salt * 2^14 clock = ~2.1 million`, so the cross-node birthday is real at high aggregate rates: ~0.2% for 100 compact ids/ms fleet-wide, ~21% at 1,000/ms, ~99.7% at 5,000/ms.

So "how do we ensure no collision" has a precise answer: **use the expanded form when you need strong cross-node uniqueness** (it keeps the whole node), and treat the **compact form as a size-for-uniqueness trade** that is safe for a single writer or a bounded fleet and rate. This contract is identical in v1 and v2; v2 changes none of the node handling.

On the *time* dimension specifically, milliseconds sound coarser than v1's 100ns, but the comparison is against the compact form actually used, and that was never 100ns: v1 folds the low 14 bits of its 100ns timestamp away, quantizing to `2^14 * 100ns = 1.6384 ms`. So v2's 1ms bucket is **finer**, and holds fewer ids:

| | time bucket | max ids/sec/node |
|---|---|---|
| full v1 (100ns, uncompacted) | 0.0001 ms | ~164 billion |
| v1 compact (1.6384 ms fold) | 1.6384 ms | ~10 million |
| **v2 compact (1 ms)** | 1.0 ms | **~16.4 million** |

If the clock sequence were random rather than a counter (worst case), the per-node birthday risk at 100k ids/sec is ~26% for v2 versus ~56% for v1 compact. Either way v2 is the safer of the two compact forms. (Reproduce the numbers with `python3 prototype/analysis.py`.)

### The compression idea, and the bigger win

A tempting idea is to spend 2 or 3 header bits on a "compression" scheme, a floating epoch or an exponent that keeps the encoded timestamp short as the years pass. It does not pay off, for a concrete reason: a compact id is `time + clock(14) + salt(7) + flag(1)`, and that **21-bit clock-plus-salt floor is not compressible** (it is pseudo-random and load-bearing for collisions). A present-day timestamp needs about 35 bits per year at millisecond resolution, so a current id is already near `(35 + 21 + 1)/8 ~= 7 bytes`, and no exponent trick shrinks the part that dominates. An exponent only helps the far-future tail, and that tail grows logarithmically: a fixed 2026 ms epoch stays at an 8-byte payload until 2166 and only then ticks to 9. Spending per-id bits, in three languages, to save under a byte a century and a half out is a bad trade. If the epoch ever does need moving, the next format version moves it, for free.

The compression worth having is the one **sortability already unlocks**. v2 ids sort by time, so in any sorted store (an LSM SSTable, a B-tree with prefix truncation, RocksDB) consecutive ids share their high-order bytes and the store keeps only the difference. Front-coding a sorted run of v2 compact ids measures like this:

| generation rate | raw wire | front-coded | saved |
|---|---|---|---|
| 10 /s | 8.00 B | 5.10 B | 36% |
| 1,000 /s | 8.00 B | 4.25 B | 47% |
| 100,000 /s | 8.00 B | 3.51 B | 56% |

The id drops to **3.5 to 5 bytes** stored, and it compresses *better* the faster you mint them, because more ids cluster into each shared time prefix. That is a general, standard, free win, and it is exactly the payoff of having made the ids sortable in the first place. The 2-3 bit scheme tries to hand-compress one id in isolation; the sorted store compresses the whole run for you, and does it better.

And it makes the whole size schedule above almost moot. The wire grows only in its high time bytes, which are exactly the bytes temporally-close ids share, so front-coding absorbs the growth: at the same 1,000 ids/sec, the raw wire climbs 8 -> 10 -> 11 bytes from 2026 to the year 50,000, while the front-coded size stays flat at **4.25 bytes** the whole way. The standalone wire jumps a byte here and there; the *stored* id does not.

| era | raw wire | front-coded |
|---|---|---|
| 2026 | 8.00 B | 4.25 B |
| 2166 | 10.00 B | 4.25 B |
| year 50,000 | 11.00 B | 4.25 B |

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
./build/cuuid_v2_demo --v1-vectors | python3 prototype/compare_v1.py   # v1 vs v2, side by side
python3 prototype/analysis.py   # collisions, uniqueness, compression, tag space
```

UUIDv6, UUIDv7, ULID, and Snowflake are implemented inline in `benchmarks/compare.cc` so the harness is self-contained; cuuid is the real library. The v2 prototype lives in `prototype/` (C++ codec, Python and JavaScript ports, and the cross-language check) and does not touch the frozen codec. Absolute ns/op will vary by machine; the ratios are the point.
