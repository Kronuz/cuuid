# Rolling out cuuid v6 (v1 stays, no new library)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Plan

## TL;DR

`cuuid` gains a v6 format ([CUUID_V6.md](CUUID_V6.md)) alongside v1; the two are told apart by the standard UUID version nibble, so no new library, no new name, no wire tag byte. Xapiand mints v6 for new ids and keeps decoding v1 forever. A `--legacy-ids` flag exists only to choose which format is **minted**; it is not needed for correctness, because reading old ids is handled by the nibble, not the flag. Existing data keeps working untouched: old `~cuuid` ids are opaque base59 that resolves regardless of format, and their canonical form still renders correctly via the v6-then-v1 decode fallback.

## Why this is not the two-library migration we first sketched

An earlier draft proposed a separate `wisp` library, a per-database format flag as a correctness requirement, and a new text sentinel. The version nibble makes all of that unnecessary: v1 is version 1, v6 is version 6, and that field, not a flag or a tag byte, selects the codec. So the plan collapses to "add a format, keep the name."

## cuuid (the library)

1. **v1 stays byte-for-byte frozen** (Mersenne Twister and all). It only ever reads and writes what it always did.
2. **Add the v6 codec beside it** (from `prototype/`), selected by the version nibble on encode and by a v6-then-v1 fallback on decode (see CUUID_V6.md). The generator mints v6 by default.
3. **Port v6 to Python and JS**, each ~40 lines and carrying **no Mersenne Twister** (v1's `mertwis.py` is never needed by v6). Prove byte-for-byte agreement with `prototype/cross_validate.sh`.
4. **Tests.** Keep a frozen v1 fixture set so its decoding provably never drifts; add the v6 cross-language vectors.

## Xapiand

1. **Mint v6 by default.** New ids are v6. `--legacy-ids` forces v1 minting for a deployment that wants to stay on the old format during transition. The flag only affects minting.
2. **Read both, dispatched by the nibble.** When a stored id is rendered to its canonical form, the decoder is chosen by the version nibble (v6-then-v1 fallback). The common encoded (`~`base59) path never decodes fields at all, so old and new ids coexist as opaque base59 blobs and resolve identically.
3. **Keep base59.** It is the format-agnostic text bridge that lets old `~cuuid` ids keep resolving with no decoder. Not changing the text encoding is what makes the rollout transparent.
4. **Per-database format metadata is a backstop, not a requirement.** For a database that is entirely one format, metadata disambiguates decode with zero guessing; for a mixed database (mid-transition) the nibble/fallback handles it.
5. **Writable path stays on glass;** honey remains an optional read-only compaction tier, orthogonal to this.

## Rollout

- **Phase 0 (ship, read-both).** Land the v6 codec and the nibble dispatch. Nothing mints v6 yet. Every reader can now decode both. No behavior change.
- **Phase 1 (mint v6).** Flip the default so new ids are v6. Old ids keep resolving (opaque base59) and rendering (nibble fallback). `--legacy-ids` is the opt-out for anyone who wants to keep minting v1.
- **Phase 2 (optional purity).** Reindex old databases to make them all-v6 where it is worth it (removes any decode ambiguity and lets a database drop the v1 dependency locally). Otherwise mixed databases keep working indefinitely.
- **Phase 3 (retire v1, eventually).** Once no live data is v1, drop the v1 codec and its Mersenne Twister. The frozen v1 fixtures stay one release longer as a tripwire.

No id ever changes representation in place, and the encoded text form is format-agnostic base59, so nothing already on disk breaks at any phase.

## Decisions to make

1. **Decode-fallback reliability** for mixed databases (metadata + reserved-marker + round-trip vs more validity bits). Tracked in CUUID_V6.md.
2. **Raw-wire minimization options** (VL length fold; salt out of band for single-id-origin databases). Tracked in CUUID_V6.md.
3. **Reindex policy** per index: active, lazy, or age-out. Likely a mix keyed on size and churn.
