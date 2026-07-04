# Architecture

`cuuid` is a small UUID value and codec. The public surface stays in the global namespace, matching Xapiand's original API, while the extraction seams live under `namespace cuuid` or macro headers.

## Model

`UUID` owns exactly 16 bytes. The string form is the usual 36-byte lowercase UUID text. UUID v1 helper methods read and write the timestamp, node, clock sequence, variant, and version fields directly in that byte array.

```text
UUID text / bytes
        |
        v
      UUID
        |
        +--> full wire form:      0x01 + 16 raw bytes
        |
        +--> condensed wire form: variable-length packed v1 fields
```

## Condensed serialisation

`serialise()` uses the condensed path only for RFC 4122 version-1 UUIDs (`variant == 0x80`, `version == 1`). Everything else uses the full 17-byte form.

The condensed path packs the v1 time, clock sequence, compact marker, and either the full node or a compact node reconstructed from a salt. `UUIDCondenser::serialise()` then strips leading zero bytes and stores the resulting length in the high bits of the first byte. `UUID::unserialise()` reverses that process and reconstructs the UUID fields.

The wire format is an invariant. Do not change tags, bit layout, byte order, or the variable-length prefix table without a separate compatibility plan.

## Seams

- `CUUID_TRACE_HEADER`: restores a host logger. Default `cuuid_trace.h` makes `L_CALL` a no-op, so `repr(...)` arguments disappear unevaluated.
- `CUUID_EXCEPTION_HEADER`: restores host exception types and `THROW`. Default `cuuid_exception.h` maps `SerialisationError` and `InvalidArgument` to standard-library exceptions.
- `CUUID_NODE_HEADER`: supplies the optional hash of the local node name used while compacting UUID v1 nodes. Default `cuuid_node.h` returns `std::nullopt`, exactly like the old code path when Xapiand had no local node.

## Invariants and suspected pre-existing bugs

- **Byte-identical wire format.** Existing serialised UUIDs must continue to decode, and newly encoded UUIDs must match the original Xapiand encoding.
- **Node salt uses bitwise OR (fixed, breaking).** `compact_crush()` computes the salt as `fnv_1a((local_node_hash | node))`. The Xapiand original used logical OR (`||`), collapsing `node` to a boolean before hashing, so every non-zero node produced one salt value. Fixed to bitwise `|` here; this changes newly-compacted UUID salt bits versus older versions (a deliberate breaking change), while reading previously-stored condensed UUIDs is unaffected.
- **No hard Xapiand dependency.** Logging, exceptions, and node identity remain optional compile-time seams.
