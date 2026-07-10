# wisp (working name): a compact, sortable, self-describing id

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Draft spec for a clean-slate successor to [cuuid](https://github.com/Kronuz/cuuid)

> **`wisp` is a working name** (a tiny, quick, ephemeral thing). Rename freely. This spec would
> live in a new repo, `Kronuz/wisp`, with cuuid kept as the frozen legacy library.

## TL;DR

`wisp` is what the validated cuuid "v2" prototype becomes once it stops being bound to cuuid's baggage, designed around one rule: **every stored byte hurts, so the wire carries entropy and nothing else.** It keeps what we proved out (a UUIDv6-compatible core, splitmix64 node reconstruction, a millisecond-since-2026 timestamp, a byte-sortable variable-length wire at its ~7-byte floor) and adds the things a clean break lets us add *without spending wire bytes*: a **sort-preserving text encoding** so ids sort by time as text too, a required **sentinel character** for O(1) id detection (like cuuid's `~`, but its own), and, in the text form only, an optional **type prefix** and **checksum**. Format versioning lives in the database metadata, not a wire byte. The wire is a strict, minimal subset: exactly the compact body we already benchmarked, length folded in.

## Why a new library instead of cuuid v2

Two hard constraints this whole investigation hit both came from *inheriting cuuid*: base59's `~` text form forbids a leading-zero byte, and v1 already uses 255 of 256 first-byte values so v1-vs-v2 cannot be told apart in-band. A separately-named library owes cuuid nothing. It picks its own text encoding (no `~`, no leading-zero rule) and its own detection sentinel, and it versions out of band instead of spending a wire byte, so both constraints simply do not exist. cuuid stays frozen and legacy; `wisp` is the default going forward.

## Design goals

1. **Minimal on the wire.** Every stored byte hurts at scale, so the binary wire sits at its information-theoretic floor (~7 bytes: time + clock + salt). Anything that costs a byte and isn't entropy is pushed out of the wire, into the text form (where chars are cheap) or into context (the database/field).
2. **Sortable** by creation time, in *both* the raw bytes and the text form.
3. **Fast to detect.** A text id must be recognizable as an id in O(1), by a single leading sentinel character, the way Xapiand's serialiser fast-path uses `~` today.
4. **Coordination-free**: no worker-id registry (unlike Snowflake).
5. **Interoperable**: the canonical 128-bit form is a valid RFC 9562 UUIDv6.
6. **Cheap and portable**: splitmix64 only, identical across C++/Python/JS, no Mersenne Twister.
7. **Future-proof**: adding `wisp` v2 later must be a non-event (the mistake cuuid made), *without* spending a wire byte on it.

## The byte budget (the governing principle)

A single id is already at its entropy floor (~57 bits ≈ 7 bytes); nothing compresses it further in isolation. So the design rule is blunt: **the wire carries entropy and nothing else.** Version, type, and checksum are real features, but each would be a dead byte on every stored id, so none of them lives in the wire:

| feature | where it lives | wire cost |
|---|---|---|
| entropy (time, clock, salt/node) | the wire | ~7 bytes (the floor) |
| format version | database/schema metadata + text sentinel | 0 bytes (a couple reserved low bits, optional) |
| type / entity | field context + text prefix | 0 bytes (or a few low bits, opt-in) |
| checksum | text form only (typos happen in text, not storage) | 0 bytes |
| fast "is-this-an-id" detection | text sentinel character | 0 wire bytes (1 text char) |

The wire stays at the floor; the text form absorbs the human-facing extras because a few characters in a rarely-stored string cost far less than a byte on every stored id.

## The three forms

A `wisp` has three representations of one logical id:

- **Canonical** (16 bytes / 36-char string): a plain **UUIDv6**. The interop form; any UUID library accepts it and it sorts by time. Used when a standard UUID is needed.
- **Binary wire** (variable, ~7 bytes): the compact storage/term form. Pure entropy, sortable, prefix-compresses. This is what lands in the index at scale, so it is minimized hardest.
- **Text** (short string): the human/API form. Carries the sentinel, optional type prefix, and optional checksum, none of which touch the wire.

## Binary wire

```text
[ time (ms/2026, MSB-first, length folded into top bits) | clock(14) | salt-or-node | flags ]
  <----------------------------------- sortable, ~7 bytes -------------------------------->
```

There is **no leading version byte, no length byte, no type byte, no checksum byte.** The wire is exactly the prototype's compact/expanded body, with the length folded into the payload's top bits (cuuid's VL trick) rather than an explicit length byte:

- **Timestamp first** (milliseconds since the 2026 epoch), MSB-first, so the bytes sort by time and share prefixes for front-coding.
- **14-bit clock**, then either the full 48-bit **node** (expanded) or the 7-bit **salt** (compact, node reconstructed with splitmix64).
- **flags** in the least-significant bits (so they never perturb time ordering): the `compact` bit, plus **2 reserved bits** for a future in-wire micro-version. Those reserved bits are the only concession to in-wire versioning, and they cost nothing (they fit in bits the layout already rounds up to).

First byte is a high timestamp byte, always nonzero, so any text encoding round-trips it. Everything about size, sortability, collisions, and front-coding is exactly what the prototype measured.

**Versioning without a wire byte.** The primary format version is the **database's** (out of band, in metadata), which is safe because a database is version-pure. The text form is **self-describing via its sentinel** (below). The 2 reserved wire bits cover a minor in-place tweak. So `wisp` is future-proof and self-describing where it matters, without the dead byte cuuid's dispatch problem might tempt you to add.

## Text form

```text
<S><sort-preserving body>[c]
 $  0j8f9k2m7q            (optional check char)      e.g.  $0j8f9k2m7q
```

- **Sentinel `<S>`** (one character, required): marks the string as a `wisp` id for the O(1) fast-path, exactly as `~` does for cuuid. It is a single, fixed, rare character (a leading `~` means cuuid; `wisp` needs its own, distinct one, e.g. `` ` `` or `$`, chosen to be rare in real text so the fast-path's negative check stays cheap). Being constant, it does not disturb text sort order.
- **Body**: **Crockford base32** by default. Its `0-9A-Z` alphabet is order-preserving, so the text sorts in byte order (time-sortable, like ULID), is case-insensitive, drops look-alike characters (no I, L, O, U), and is URL/JSON/filename safe. An **order-preserving base64** variant (custom ASCII-sorted alphabet, ~11 chars for 7 bytes vs base32's ~12) is available where text length matters more than legibility; plain RFC base64url is rejected because its alphabet is not order-preserving and would throw text sortability away.
- **Check char `[c]`** (optional): one trailing base32 symbol for typo detection, computed at render time. Never stored in the wire.

An optional human **type prefix** (`doc_`, `idx_`) can precede the sentinel for legibility (`doc_$0j8f9k2m7q`), derived from field context, again never touching the wire.

## Fast id detection (Xapiand fast-path)

Xapiand's value serialiser guesses a string's type and, on a miss, throws, so the negative path is expensive. It avoids that with a fast pre-check (`src/serialise.cc:959`): a string is *maybe typed* only if it contains one of `0-9 - ~ { : (`; otherwise it is text/keyword immediately. And an encoded UUID is recognized in O(1) by `uuid.front() == '~' && size >= 7` (`serialise.cc:90`).

`wisp` preserves this exactly, with its own sentinel:
- Add the `wisp` sentinel to the fast pre-check's guarded character set, so a `wisp` id is flagged *maybe typed* and a plain string still short-circuits to text/keyword.
- Recognize a `wisp` id in O(1) by `front() == <S> && size >= <min>`, mirroring the `~` check.

Because the sentinel is distinct from `~`, the fast-path tells `wisp` from cuuid (and from every other typed grammar) in a single character compare, with no wire cost.

## Reserved ids (well-known sentinels)

Some use cases want a fixed, recognizable id: an empty/default value, or the bounds of a range scan. `wisp` reserves a small set of **well-known ids** with short, deliberate text names, so you get `$nil` instead of the lucky-accident `~notmet` cuuid stumbled into.

Because a `wisp` is UUIDv6-compatible, it adopts RFC 9562's two reserved UUIDs directly, which also makes them interoperable and correctly ordered:

| name | value | sorts | use |
|---|---|---|---|
| `$nil` | `00000000-0000-0000-0000-000000000000` (all zeros, RFC Nil) | first | empty / unset / "no id" / lower bound |
| `$max` | `ffffffff-ffff-ffff-ffff-ffffffffffff` (all ones, RFC Max) | last | upper bound of a range scan / sentinel end |

- **They are outside the generated space.** A real `wisp` is version 6; the Nil's version nibble is 0 and the Max's is F, so the generator can never produce either by accident. They are pure sentinels.
- **Text form is the name, not the bytes.** Encoding recognizes a reserved value and emits `<S>nil` / `<S>max` (the literal short name after the sentinel); parsing maps those names back to the reserved value. So they read as `$nil` / `$max`, short and memorable, while still being detected by the same one-character fast-path.
- **They sort correctly by value.** Nil is all-zeros (sorts first, so it doubles as `$min`), Max is all-ones (sorts last), which is exactly what range bounds need. Only the *text spelling* of these specific ids is a name rather than the base32 body; the underlying bytes still order correctly.
- **The namespace is extensible.** More well-known ids can be reserved from the same non-version-6 space if a use case needs them (a `$root`, a per-deployment constant), each a name in the registry. Keep the set small; `$nil` and `$max` cover most needs.

This is a text/registry feature, so it costs nothing on the wire for normal ids.

## Properties, by where they live

In the wire (entropy only):
- **Time + clock + node/salt**, sortable, ~7 bytes, at the floor.
- **2 reserved flag bits** for a future micro-version (free).

In the text form (chars, never wire bytes):
- **Sentinel** for O(1) detection.
- **Sort-preserving encoding**.
- **Checksum** (optional).
- **Type prefix** (optional).

In context (database / schema / field):
- **Format version** (database metadata).
- **Entity type** (field), unless an id must travel context-free, in which case a few opt-in low bits or a text prefix carry it.

Opt-in modes (flags / generator config, mostly free or bit-level):
- **Compact vs expanded**: 7-bit salt (small, bounded-fleet) or full 48-bit node (strong cross-node).
- **Monotonic**: clock as a strict per-writer counter.
- **Fixed-length text**: zero-pad to a constant width.
- **Unguessable variant**: random bits instead of the deterministic node (costs compactness; for security-sensitive ids).

## What carries over unchanged from the prototype

The prototype (`Kronuz/cuuid/prototype/`) already validated the load-bearing parts, and `wisp` inherits them verbatim:
- splitmix64 node reconstruction (encode/decode ~10 ns, vs cuuid's ~900 ns Mersenne Twister).
- Millisecond-since-2026 timestamp (the size schedule and its front-coding absorption).
- v6 canonical sortability, and the ~1ms wire sort floor.
- The collision model (within-node clock counter; cross-node 48-bit node expanded / 7-bit salt compact; hashed salt).
- Cross-language byte-identical C++/Python/JS (the ports and `cross_validate.sh`).


`wisp` is those bytes, plus the header, type, checksum, and a sort-preserving text encoding.

## Decisions to make

1. **Name.** `wisp` is a placeholder.
2. **The sentinel character.** Which single rare character marks a `wisp` text id (cuuid keeps `~`)? It must be added to Xapiand's fast-path guarded set (`serialise.cc:959`) and used for the O(1) `front() == <S>` check. Candidates: `` ` `` or `$` (rare in text, not already a query operator). This is the one required text character.
3. **Default text encoding.** Crockford base32 (human-friendly, sortable, ~12 chars) vs order-preserving base64 (more compact, ~11 chars, sortable). Recommend base32 default, base64 as a size-first option.
4. **The 2 reserved wire bits.** Keep them for a future in-wire micro-version, or reclaim them (they are essentially free either way). Recommend keeping.
5. **Type handling.** Contextual (field implies type) with an optional text prefix, versus a few opt-in low bits for context-free typed ids. Recommend contextual + text prefix, no wire cost, unless context-free typing is a real requirement.
6. **Checksum.** Ship the text checksum char in v1, or defer. It is text-only (zero wire cost), so cheap to include.
7. **Which opt-in modes ship in v1** (compact/expanded, monotonic, fixed-length, unguessable) versus reserved for later.
8. **Reserved-id set.** Ship `$nil` and `$max` (recommended, RFC-backed); reserve any others (`$root`, per-deployment constants) only if a concrete use case needs them.
