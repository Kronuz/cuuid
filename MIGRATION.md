# Making v2 the default, v1 the legacy (a migration plan)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Plan, pending a few decisions (see the end)

## TL;DR

Ship v2 as a second codec next to v1, generate v2 for all new ids, and keep v1 as a read-only legacy decoder. Version the format **per database / per schema, out of band**, not with an in-band tag byte, because a byte-level tag provably cannot work (see below). New databases are v2-only; existing databases stay v1 and are either migrated by re-indexing or left to age out. Once no live database is v1, delete the v1 codec (its Mersenne Twister with it). No id ever changes representation in place, so nothing already on disk breaks.

## The one constraint that shapes everything

We proved earlier that you cannot distinguish v1 and v2 by the wire's first byte. v1 uses **255 of the 256** first-byte values; the only free one is `0x00`, and `0x00` is exactly the byte base59 (Xapiand's `~` text form) drops. So there is no leading tag that is both v1-safe and text-safe. Versioning therefore lives **outside the id's bytes**: in the database/schema metadata for stored ids, and in a distinct text prefix for the `~` form. This is standard practice (it is how Xapian already versions glass vs honey), and it is cleaner than any bit-stealing scheme.

A consequence worth stating up front: we do **not** support mixing v1 and v2 ids inside one database. Each database is version-pure. That removes all the hard cases.

## cuuid (the library)

1. **Keep v1 byte-for-byte.** `uuid.{hh,cc}` stays as it is, Mersenne Twister and all. v1 is frozen; it only ever reads and writes what it always did.
2. **Add the v2 codec beside it**, ported from `prototype/cuuid_v2.hh`: UUIDv6 canonical, splitmix64 node reconstruction, millisecond-since-2026 timestamp, variable-length wire. Select the codec with an explicit `format` enum (`V1`, `V2`), never by guessing from bytes.
3. **Generator defaults to v2.** `UUIDGenerator` mints v2 ids for new work; a legacy flag can still produce v1 if some caller genuinely needs it during the transition.
4. **`serialise(format)` / `unserialise(bytes, format)` take the version explicitly.** The caller supplies it from context (the database's format version). There is no in-band sniffing.
5. **Text form.** v1 keeps `~` + base59. v2 gets its **own sentinel** (a different leading character) so a bare text id is self-describing regardless of which database it came from. Both are base59 of a nonzero-first-byte wire, so both round-trip through text.
6. **Port to all three languages.** The wire is a C++/Python/JavaScript contract. Port the v2 codec to the Python and JS contribs; because splitmix64 is fixed-width integer arithmetic, each port is ~40 lines and **deletes its Mersenne Twister** (`contrib/python/cuuid/mertwis.py` goes away in the v2 path). Reuse `prototype/cross_validate.sh` to prove byte-for-byte agreement.
7. **Tests.** Round-trip both codecs; keep a frozen v1 fixture set so we can prove v1 decoding never drifts; add the v2 cross-language vectors.

## Xapiand

1. **Record a cuuid format version in the database/schema metadata**, alongside the existing backend version. A database created by the new Xapiand is stamped `cuuid: v2`; an existing one reads as `v1`.
2. **Dispatch the ID codec on that version.** When Xapiand serialises or parses the `Q`-prefixed document-id term, it uses the codec for that database's version. Because each database is version-pure, this is a single lookup, not a per-id decision.
3. **Text I/O** (query strings, JSON document ids) uses the sentinel from the library: `~...` is v1, the v2 sentinel is v2. A user-supplied id is decoded by its prefix; an id we emit uses the database's version.
4. **Writable path stays on glass.** Nothing here needs honey. (Honey remains a separate, optional compaction tier; it is read-only and orthogonal to this.)
5. **Replication and WAL carry the version** the same way they carry the backend version, so a replica reconstructs the same codec.

## Migration and deprecation

- **Phase 0 (ship, no behavior change).** Land the v2 codec in cuuid and Xapiand, defaulting **new databases** to v2. Existing databases keep writing v1. Every reader can now read both. This is the only step that must happen before any v2 id exists anywhere.
- **Phase 1 (v2 is the default).** All newly created databases are v2. Old databases are still v1 and fully readable and writable by the v1 codec.
- **Phase 2 (drain v1).** Migrate old databases by **re-indexing** their documents into fresh v2 databases (a normal reindex; ids are regenerated as v2, or the document keeps its logical id and only the storage representation changes). Alternatively, let short-lived indices age out on their own. Track how many live databases are still v1.
- **Phase 3 (drop v1).** Once no live database reports `cuuid: v1`, delete the v1 codec from the library and the contribs, taking the Mersenne Twister with it. The frozen v1 fixtures stay in the test suite one release longer as a tripwire, then go too.

Because ids are only ever *rewritten* (during reindex), never reinterpreted in place, there is no window where an id decodes to two different values. That is the property the whole plan is built to preserve.

## Decisions to make

1. **v2 text sentinel.** Which character marks a v2 text id (so `~` stays v1)? Needs to be one Xapiand's query and JSON layers treat as an id prefix and that is not otherwise meaningful.
2. **Length encoding.** Fold the length into the payload's top bits like v1's VL table (8-byte steady wire, = v1) or keep the prototype's explicit length byte (simpler, one byte larger)? This is a forever-byte, so probably worth folding.
3. **Nil special-case.** Port v1's clean all-zero node (`0x010000000000`) into v2's `reconstruct_node`, so a v2 nil is a recognizable value rather than a splitmix node? Cosmetic, cheap.
4. **Migration strategy per index.** Active reindex, lazy read-old-write-new, or age-out only? Likely a mix keyed on index size and churn.
5. **Same-database coexistence.** Confirm we never need v1 and v2 in one database (the plan assumes not). If some shared/global index must accept both, that reopens the in-band dispatch problem and needs its own answer.
