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

### v1 node expander

A compact v1 node is the first two `std::mt19937(seed)` outputs, `(out0 << 32) | out1`. Building the engine and drawing twists all 624 words, but output k of the first generation depends only on state words k, k+1 and k+397 (k, k+1, k+397, k+398 for outputs 0 and 1), so `expand_mt_compute` runs the init recurrence `mt[i] = 1812433253 * (mt[i-1] ^ (mt[i-1] >> 30)) + i` up to word 398 (a serial chain with no closed form, so this is the floor) and applies the twist and tempering to those words only. A small thread-local direct-mapped cache (8 entries, keyed by seed) covers `generate()` followed by `serialise()` of the same id. `test/test_mt.cc` compares it with the real engine over many seeds and checks the wire bytes against a frozen full-engine expander. v6's `generate()` keeps its monotonic-time guarantee with a lock-free CAS on an atomic.

The wire format is an invariant. Do not change tags, bit layout, byte order, or the variable-length prefix table without a separate compatibility plan.

## Seams

- `CUUID_TRACE_HEADER`: restores a host logger. Default `cuuid_trace.h` makes `L_CALL` a no-op, so `repr(...)` arguments disappear unevaluated.
- `CUUID_EXCEPTION_HEADER`: restores host exception types and `THROW`. Default `cuuid_exception.h` maps `SerialisationError` and `InvalidArgument` to standard-library exceptions.
- `CUUID_NODE_HEADER`: supplies the optional hash of the local node name used while compacting UUID v1 nodes. Default `cuuid_node.h` returns `std::nullopt`, which behaves like Xapiand with no local node.

## Invariants

- **Byte-identical wire format.** Existing serialised UUIDs must continue to decode, and newly encoded UUIDs must match the Xapiand encoding, apart from the salt below.
- **Node salt uses bitwise OR.** `compact_crush()` computes the salt as `fnv_1a((local_node_hash | node))`, so the salt depends on the node. Xapiand uses logical OR (`||`), which gives every non-zero node the same salt, so a v1 id compacted here carries different salt bits than one compacted by Xapiand. Both decode: the stored salt is read back as is.
- **No hard Xapiand dependency.** Logging, exceptions, and node identity remain optional compile-time seams.
