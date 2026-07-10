# cuuid v6: the fast condensed UUID

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Draft spec for a second format inside [cuuid](https://github.com/Kronuz/cuuid)

## TL;DR

`cuuid` keeps its name and gains a second wire format, **v6**, beside the existing **v1**. On encode they are told apart by the standard UUID **version nibble** (1 vs 6); on decode the running mode decides (default v6, `--legacy-ids` pure v1). v6 is v1 **minus the Mersenne Twister**: it reconstructs the compacted node with splitmix64 instead of `std::mt19937`, stores the timestamp as milliseconds since a 2026 epoch, and yields a UUIDv6 canonical form. Its win is **speed**, roughly **120x faster decode** (~7 ns vs ~900 ns, paid on every read), plus a cleaner, no-Mersenne-Twister port in every language. Raw-wire size and index sortability are **the same as v1** (v1's compact wire already sorts), so v6 is a faster, cleaner version of the same idea, not a smaller one. New ids are minted v6 by default; old v1 ids still resolve by their bytes, at the cost of a documented change to how their canonical form renders (see `upgrade.md`).

## Why v6 (the honest value)

The measurements from `prototype/` and `COMPARISON.md`, stated without inflation:

- **Decode speed (the prize).** v1 runs `std::mt19937` in `calculate_node()` on every decode of a compacted id: **~843 to 1043 ns**. v6's splitmix64 does the same job in **~7 ns**. For a search engine that reads billions of ids, that is the real saving: CPU, latency, cost, on every read.
- **Portability.** splitmix64 is fixed-width integer arithmetic, identical across C++/Python/JS by construction, so every port drops its hand-rolled Mersenne Twister (`contrib/python/cuuid/mertwis.py`) for four lines.
- **Cleaner semantics.** Milliseconds instead of 100 ns ticks, no XOR fold, no clock scrambling; a clean 1 ms sort granularity rather than v1's scrambled 1.64 ms floor.
- **Marginal size.** v6 is a 4-byte payload for about a year near its 2026 epoch, and stays 8 bytes where v1 slowly creeps to 9 past ~2100.

What v6 is **not**: it is not smaller on the raw wire at steady state (both hit the ~8-byte entropy floor), and it does not improve stored sortability, because **v1's compact wire already sorts by time** (measured 1.0000) and front-codes the same. Only v1's canonical 16-byte form fails to sort, and Xapiand stores the compact wire, not the canonical. So the pitch is speed and cleanliness with size held constant.

## Telling v1 from v6 (the version nibble)

The discriminator is a standard UUID field, and it is used on **encode**, where it is exact and free: a canonical / logical UUID carries its version in byte 6's high nibble, so **1 selects the v1 codec, 6 selects v6.** A `~`-encoded input needs no codec at all: it is base59-decoded to bytes and stored opaque.

**Decode is deliberately simple: whichever codec the deployment is running decodes everything.** In the default mode that is v6; under `--legacy-ids` it is v1, and the v6 engine is never touched. There is no per-id sniffing and no v6-then-v1 fallback.

The consequence is an accepted, documented breaking change: in the default mode, an **old v1 id decoded to its canonical form is read through v6 and renders a different UUID string.** This is fine because it only affects the field-decoding reprs (`vanilla`/`urn`/`guid`); the common `~`base59 and opaque-byte-lookup paths never decode fields, so old ids still resolve and still render as their original `~` string. A deployment that cannot accept the change runs `--legacy-ids` (pure v1, no break). The change is documented in Xapiand's `upgrade.md` for the 0.40.0 to 1.0.0 bump. This is why the name stays `cuuid`: v6 is not a different species, just the next version, selected by mode, disambiguated on encode by a field that already exists.

## The v6 wire

```text
[ time (ms since 2026, MSB-first, length folded into top bits) | clock(14) | salt-or-node | flags ]
  <------------------------------------ sortable, ~8 bytes ------------------------------------->
```

- **Timestamp first** (milliseconds since 2026), MSB-first, so the bytes sort by time and share prefixes for front-coding.
- **14-bit clock**, then either the full 48-bit **node** (expanded) or the 7-bit **salt** (compact, node reconstructed with splitmix64, salt derived like v1 as `xor_fold(fnv_1a(node), 7)`).
- **flags** in the least-significant bits (so they never perturb time ordering): just the `compact` bit. No version or marker bits are needed on the wire, because the format is selected by mode (default v6 / `--legacy-ids` v1) and, on encode, by the canonical version nibble.
- **Length folded into the top bits** (cuuid's VL trick), so there is no separate length byte. First byte is a high timestamp byte, always nonzero.

Everything about size, sortability, collisions, and front-coding is exactly what the prototype measured.

## Minimizing the raw wire

A single id is at its entropy floor (~57 bits), so nothing compresses it further in isolation. The genuine levers:

| variant | ~2027 | ~2100 | cost |
|---|---|---|---|
| **SAFE (default)**: time + clock(14) + salt(7) + flag | 8 B | 8 B | none (the floor); VL-folded, free vs an explicit length byte |
| narrow clock(10), no salt | 6 B | 7 B | less burst headroom (1024 ids/ms), no cross-node uniqueness |
| varint clock, no salt/flag | 5 B | 6 B | complexity; cross-node uniqueness gone |

The one free win over the prototype is **folding the length byte** (9 -> 8), which the default takes. Below that is only opt-in trades of collision headroom or cross-node uniqueness, offered as generator modes, not defaults. (A "salt out of band" variant, storing the node identity once per single-id-origin database, could reach 7 bytes losslessly, but it adds enough plumbing that it is not worth it as a default.)

And the real minimization is not on the standalone wire at all: because both v1 and v6 compact wires sort by time, a prefix-compressing store front-codes them to **~4.25 bytes per id**, flat across eras. The standalone wire is near-irreducible; the store is where the bytes actually shrink.

## Text form

`~` + base59, **the same as v1**, kept deliberately: base59 is a format-agnostic bridge between text and bytes, so `~<base59>` round-trips at the byte level and an old `~cuuid` id keeps resolving without any decoder. The format (v1 vs v6) only matters when decoding to the canonical form, which the running mode selects. base59 is not order-preserving, so text ids do not sort, but the **bytes** do, and the store sorts on bytes, so nothing is lost for indexing. (An order-preserving text encoding could be a later, opt-in variant if text-sorting is ever needed; it is not needed for Xapiand.)

### Reserved ids (well-known values)

v6 adopts RFC 9562's reserved UUIDs with short text names, so you get a deliberate `~nil` instead of the lucky `~notmet`:

| name | value | sorts | use |
|---|---|---|---|
| `~nil` | `00000000-0000-0000-0000-000000000000` (RFC Nil) | first | empty / unset / lower bound (doubles as `~min`) |
| `~max` | `ffffffff-ffff-ffff-ffff-ffffffffffff` (RFC Max) | last | upper bound of a range scan |

They are outside the generated space (version nibbles 0 and F, never 1 or 6), render as the literal short name after `~` via a small registry, and sort correctly by value. Text/registry only, zero wire cost.

## What carries over unchanged from the prototype

The prototype (`prototype/`) already validated the load-bearing parts, and v6 inherits them verbatim:
- splitmix64 node reconstruction (encode/decode ~10 ns).
- Millisecond-since-2026 timestamp and the size schedule.
- v6 canonical sortability and the ~1 ms wire sort floor.
- The collision model (within-node clock counter; cross-node 48-bit node expanded / 7-bit hashed salt compact).
- Cross-language byte-identical C++/Python/JS (the ports and `cross_validate.sh`).

## Decisions to make

1. **Length fold.** Resolved: fold the length into the top bits (v1's VL table), so the compact wire is 8 bytes, not 9. Implemented and validated in the prototype (`encode_vl`/`decode_vl`), round-trips clean, stays sortable within an era.
2. **Reserved-id set.** Ship `~nil` and `~max`; reserve others only on demand.
3. **Text encoding.** Keep base59 (recommended, for old-id compatibility); leave an order-preserving encoding as a future opt-in.
