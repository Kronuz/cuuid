# Rolling out cuuid v6 (v1 stays, no new library)

**Author:** [German Mendez Bravo](mailto:german.mb@gmail.com)
**Date:** 2026-07-10
**Status:** Plan

## TL;DR

`cuuid` gains a v6 format ([CUUID_V6.md](CUUID_V6.md)) alongside v1; on encode they are told apart by the standard UUID version nibble, so no new library, no new name, no wire tag byte. Xapiand mints v6 for new ids. A `--legacy-ids` flag puts a deployment in **pure v1-only mode**: it mints v1 *and* bypasses the v6 encode/decode engine completely, so behavior is byte-for-byte identical to today's cuuid (and a guaranteed-safe escape hatch). In the default mode the running codec is v6; old ids still resolve by their bytes (opaque base59), at the cost of a documented change to how their canonical form renders. That break is intentional for the 0.40.0 to 1.0.0 bump and is written up in Xapiand's `upgrade.md`.

## Why this is not the two-library migration we first sketched

An earlier draft proposed a separate `wisp` library, a per-database format flag as a correctness requirement, and a new text sentinel. The version nibble makes all of that unnecessary: v1 is version 1, v6 is version 6, and that field, not a flag or a tag byte, selects the codec. So the plan collapses to "add a format, keep the name."

## cuuid (the library)

1. **v1 stays byte-for-byte frozen** (Mersenne Twister and all). It only ever reads and writes what it always did.
2. **Add the v6 codec beside it** (from `prototype/`, VL-folded to 8 bytes), selected by the version nibble on encode and by the running mode on decode (default v6, `--legacy-ids` v1). The generator mints v6 by default.
3. **Port v6 to Python and JS**, each ~40 lines and carrying **no Mersenne Twister** (v1's `mertwis.py` is never needed by v6). Prove byte-for-byte agreement with `prototype/cross_validate.sh`.
4. **Tests.** Keep a frozen v1 fixture set so its decoding provably never drifts; add the v6 cross-language vectors.

## Xapiand

1. **Mint v6 by default.** New ids are v6. **`--legacy-ids` selects pure v1-only mode**: it mints v1 and takes the v6 engine out of the loop entirely, no v6 encode, no v6 decode, no fallback, so the deployment is byte-for-byte the old cuuid. It is both the transition opt-out and the safe rollback if anything about v6 misbehaves.
2. **Decode with the running codec.** In the default mode v6 decodes everything; a stored id rendered to its canonical form is read as v6. The common encoded (`~`base59) path never decodes fields at all, so old and new ids coexist as opaque base59 blobs and resolve identically. Only the field-decoding reprs (`vanilla`/`urn`/`guid`) see the break, and only for pre-existing v1 ids.
3. **Keep base59.** It is the format-agnostic text bridge that lets old `~cuuid` ids keep resolving with no decoder. Not changing the text encoding is what makes lookups transparent across the upgrade.
4. **No per-database format flag is required.** Databases are not marked; the deployment-wide mode (default v6 / `--legacy-ids` v1) selects the codec. This is simpler than the earlier per-database-metadata idea and is what the accepted break buys us.
5. **Writable path stays on glass;** honey remains an optional read-only compaction tier, orthogonal to this.

## Rollout

- **Phase 0 (ship the codec).** Land the v6 codec and encode-side nibble selection. Nothing mints v6 yet; the default codec is still effectively v1. No behavior change.
- **Phase 1 (mint v6).** Flip the default so new ids are v6 and v6 becomes the running decoder. Old ids keep resolving (opaque base59); their canonical rendering changes, per the documented break. `--legacy-ids` is the opt-out for anyone who wants to stay pure v1.
- **Phase 2 (optional cleanup).** For deployments that use a field-decoding repr and care that old ids render their original canonical form, reindex old documents so their ids are re-minted as v6. Otherwise nothing needs doing; old ids keep resolving.
- **Phase 3 (retire v1, eventually).** Once no live data is v1, drop the v1 codec and its Mersenne Twister. The frozen v1 fixtures stay one release longer as a tripwire.

No id ever changes representation in place, and the encoded text form is format-agnostic base59, so lookups never break; only the canonical rendering of pre-existing ids changes, as documented.

## Decisions to make

1. **Which reprs are in use.** The break only touches field-decoding reprs (`vanilla`/`urn`/`guid`) for old ids; confirm the deployment's `uuid_repr` so the blast radius is known (encoded/`~` is unaffected). Tracked in the Xapiand upgrade notes.
2. **Raw-wire minimization options** (VL length fold; salt out of band for single-id-origin databases). Tracked in CUUID_V6.md.
3. **Reindex policy** per index: active, lazy, or age-out. Likely a mix keyed on size and churn.
