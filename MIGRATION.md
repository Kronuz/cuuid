# Making wisp the default, cuuid the legacy (a migration plan)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Plan, pending a few decisions (see the end)

## TL;DR

Ship the successor as its own library, [`wisp`](WISP_SPEC.md) (working name), and keep [`cuuid`](https://github.com/Kronuz/cuuid) frozen as the read-only legacy. Xapiand defaults **new** databases to `wisp` and offers a `--legacy-cuuid` opt-out; existing `cuuid` databases keep working and are detected automatically from their metadata. Version lives **per database, out of band**, never as an in-band wire byte, because a byte-level tag provably cannot work (below) and because every wire byte hurts. New databases are `wisp`-only; old ones stay `cuuid` and are migrated by re-indexing or left to age out. Once no live database is `cuuid`, delete the `cuuid` dependency (its Mersenne Twister with it). No id ever changes representation in place, so nothing already on disk breaks.

## The one constraint that shapes everything

We proved earlier that you cannot distinguish two id formats by the wire's first byte. `cuuid` uses **255 of the 256** first-byte values; the only free one is `0x00`, and `0x00` is exactly the byte base59 (the `~` text form) drops. So there is no leading tag that is both cuuid-safe and text-safe, and adding a version byte to `wisp` would also violate the "every stored byte hurts" rule. Versioning therefore lives **outside the id's bytes**: in the database/schema metadata for stored ids, and in each library's distinct text sentinel for the text form. This is standard practice (it is how Xapian already versions glass vs honey), and it is cleaner and cheaper than any bit-stealing scheme.

A consequence worth stating up front: we do **not** support mixing `cuuid` and `wisp` ids inside one database. Each database is format-pure. That removes all the hard cases.

## The two libraries

1. **`cuuid` stays byte-for-byte frozen.** `uuid.{hh,cc}` and the two contrib ports are untouched, Mersenne Twister and all. It only ever reads and writes what it always did. It becomes read-only legacy: no new `cuuid` databases are created once `wisp` ships.
2. **`wisp` is a new repo** (`Kronuz/wisp`), built from `prototype/cuuid_v2.hh` and specified in `WISP_SPEC.md`: UUIDv6 canonical, splitmix64 reconstruction, millisecond-since-2026 timestamp, a minimal ~7-byte sortable wire, an order-preserving text encoding, and its own detection sentinel. The generator mints `wisp` by default.
3. **No in-band sniffing in either.** `serialise` / `unserialise` act on a known format supplied by context (the database's format), never guessed from bytes.
4. **Port `wisp` to all three languages.** The wire is a C++/Python/JavaScript contract. Because splitmix64 is fixed-width integer arithmetic, each port is ~40 lines and carries **no Mersenne Twister** (the reason `cuuid`'s Python port had to ship `mertwis.py`; `wisp` never does). Reuse `prototype/cross_validate.sh` to prove byte-for-byte agreement.
5. **Tests.** Keep a frozen `cuuid` fixture set so its decoding provably never drifts; add the `wisp` cross-language vectors.

## Xapiand

1. **Auto-detect the format from database metadata.** A database records which id format it uses (alongside the backend version). An existing database reads as `cuuid` and keeps using it; a newly created one is stamped `wisp`.
2. **`--legacy-cuuid` is the explicit override**, for an operator who wants new databases to keep using `cuuid` during the transition. The default for new databases is `wisp`; detection, not the flag, protects existing data, so forgetting the flag can never turn a `cuuid` database into a `wisp` one.
3. **Dispatch the ID codec on the database's format.** When Xapiand serialises or parses the `Q`-prefixed document-id term, it uses that database's codec. Because each database is format-pure, this is a single lookup, not a per-id decision.
4. **Text I/O keeps the O(1) fast-path.** Xapiand recognizes an id string in one character compare today (`serialise.cc:90`, `uuid.front() == '~'`) and short-circuits non-typed strings with the guarded-character pre-check (`serialise.cc:959`). `wisp` adds its own sentinel to that guarded set and its own `front() == <S>` check, so `~...` means `cuuid`, the `wisp` sentinel means `wisp`, and a plain string still skips both. A user-supplied id is decoded by its sentinel; an id we emit uses the database's format.
5. **Writable path stays on glass.** Nothing here needs honey. (Honey remains a separate, optional compaction tier; it is read-only and orthogonal to this.)
6. **Replication and WAL carry the format** the same way they carry the backend version, so a replica reconstructs the same codec.

## Migration and deprecation

- **Phase 0 (ship, no behavior change).** Land `wisp` and wire it into Xapiand, defaulting **new databases** to `wisp` (detection keeps existing databases on `cuuid`). Every reader can now read both. This is the only step that must happen before any `wisp` id exists anywhere.
- **Phase 1 (`wisp` is the default).** All newly created databases are `wisp`. Old databases are still `cuuid` and fully readable and writable by the frozen `cuuid` codec.
- **Phase 2 (drain `cuuid`).** Migrate old databases by **re-indexing** their documents into fresh `wisp` databases (a normal reindex; ids are regenerated, or the document keeps its logical id and only the storage representation changes). Alternatively, let short-lived indices age out on their own. Track how many live databases are still `cuuid`.
- **Phase 3 (drop `cuuid`).** Once no live database reports the `cuuid` format, remove the `cuuid` dependency, taking the Mersenne Twister with it. The frozen `cuuid` fixtures stay in the test suite one release longer as a tripwire, then go too.

Because ids are only ever *rewritten* (during reindex), never reinterpreted in place, there is no window where an id decodes to two different values. That is the property the whole plan is built to preserve.

## Decisions to make

1. **The `wisp` sentinel.** Which character marks a `wisp` text id (so `~` stays `cuuid`)? It must join Xapiand's guarded pre-check set and be rare in real text. Tracked in `WISP_SPEC.md`.
2. **`wisp` text encoding + minimal wire details** (order-preserving base32 vs base64, the folded length): owned by `WISP_SPEC.md`.
3. **Migration strategy per index.** Active reindex, lazy read-old-write-new, or age-out only? Likely a mix keyed on index size and churn.
4. **Same-database coexistence.** Confirm we never need `cuuid` and `wisp` in one database (the plan assumes not). If some shared/global index must accept both, that reopens the in-band dispatch problem and needs its own answer.
