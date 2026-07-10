#!/usr/bin/env python3
"""Collision and compression analysis for the cuuid v2 prototype.

Answers two questions the v2 design raises:
  1. Does storing the timestamp as milliseconds (v2) hurt collision resistance versus
     v1's compact form (which already quantized to 1.6384ms via its 14-bit fold)?
  2. Is a 2-3 bit "compression" header worth it, versus the prefix compression that
     sortable ids already unlock in a sorted store?

Run: python3 analysis.py   (imports the Python port for encoding)
"""

from __future__ import annotations

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python"))
import cuuid_v2 as v2  # noqa: E402

CLOCK = 1 << 14  # 14-bit clock_seq space


def collision_report() -> None:
    print("== Collisions: v2 (1ms) vs v1 compact (1.6384ms) vs full v1 (100ns) ==")
    print(
        "A compact id is unique per (time bucket, clock_seq); the salt is fixed per node."
    )
    print("Max ids/sec/node before the 14-bit clock_seq is exhausted:")
    for name, bucket_s in [
        ("full v1 (100ns)", 100e-9),
        ("v1 compact (1.6384ms)", 16384 * 100e-9),
        ("v2 compact (1ms)", 1e-3),
    ]:
        print(f"  {name:<24} {CLOCK / bucket_s / 1e6:>10,.1f} M ids/sec/node")
    print("\nBirthday risk if clock_seq were random (worst case), per node:")
    print(f"{'rate/s':>10} | {'v1 compact':>12} | {'v2 compact':>12}")
    for rate in (1e3, 1e4, 1e5):
        risks = []
        for bucket_s in (16384 * 100e-9, 1e-3):
            m = rate * bucket_s
            risks.append(1 - math.exp(-(m * m) / (2 * CLOCK)))
        print(f"{int(rate):>10} | {risks[0]:>11.1%} | {risks[1]:>11.1%}")
    print(
        "Expanded ids keep the full 48-bit node, so cross-node uniqueness is unchanged.\n"
    )


def front_code_bytes(wires: list[bytes]) -> float:
    """Average bytes/id keeping only (shared-prefix-length + differing suffix) of a sorted run."""
    prev = b""
    kept = 0
    for w in sorted(wires):
        s = 0
        while s < len(prev) and s < len(w) and prev[s] == w[s]:
            s += 1
        kept += 1 + (len(w) - s)
        prev = w
    return kept / len(wires)


def compression_report() -> None:
    print("== Compression: prefix-coding of a SORTED run of v2 compact ids ==")
    print(
        "The win a sorted store (LSM/B-tree) gives for free, because the ids sort by time."
    )
    random.seed(1)
    start_ms = v2.EPOCH_2026_MS + 190 * 86400 * 1000  # mid-2026
    print(f"{'rate/s':>10} | {'raw':>7} | {'front-coded':>12} | {'saved':>6}")
    for rate in (10, 100, 1000, 10000, 100000):
        wires = []
        per_ms = rate / 1000.0
        for i in range(20000):
            greg = v2.GREG_EPOCH_100NS + (start_ms + int(i / per_ms)) * 10000
            t, c, n = v2.crush(
                greg, random.getrandbits(14), 0x020000000000 | random.getrandbits(40)
            )
            wires.append(v2.encode(t, c, n))
        raw = sum(len(w) for w in wires) / len(wires)
        fc = front_code_bytes(wires)
        print(f"{rate:>10} | {raw:>5.2f} B | {fc:>10.2f} B | {1 - fc / raw:>5.0%}")
    print(
        "A current standalone compact id floors near 7 bytes (21-bit clock+salt is not"
    )
    print(
        "compressible); a sorted store front-codes the run to ~4-5 bytes, better at higher rate."
    )


def tag_space_report() -> None:
    print("== Safe v2 tag: exhaustive over all 256 first-byte values (not sampled) ==")
    # v1's length-prefix table (invariant, mirrored from uuid.cc); index 0..12 = lengths 4..16.
    vl = [
        [(0x1C, 0xFC), (0x1C, 0xFC)],
        [(0x18, 0xFC), (0x18, 0xFC)],
        [(0x14, 0xFC), (0x14, 0xFC)],
        [(0x10, 0xFC), (0x10, 0xFC)],
        [(0x04, 0xFC), (0x40, 0xC0)],
        [(0x0A, 0xFE), (0xA0, 0xE0)],
        [(0x08, 0xFE), (0x80, 0xE0)],
        [(0x02, 0xFF), (0x20, 0xF0)],
        [(0x03, 0xFF), (0x30, 0xF0)],
        [(0x0C, 0xFF), (0xC0, 0xF0)],
        [(0x0D, 0xFF), (0xD0, 0xF0)],
        [(0x0E, 0xFF), (0xE0, 0xF0)],
        [(0x0F, 0xFF), (0xF0, 0xF0)],
    ]

    def valid_v1(b: int) -> bool:
        if b == 0x01:  # full form is exactly 0x01
            return True
        q = (
            1 if (b & 0xF0) else 0
        )  # mirrors unserialise_condensed's first-byte dispatch
        return any(pref == (b & mask) for pref, mask in (vl[i][q] for i in range(13)))

    invalid = [b for b in range(256) if not valid_v1(b)]
    print(f"  v1 uses {256 - len(invalid)} of 256 possible first bytes.")
    print(
        f"  bytes NO v1 wire can start with: {' '.join(f'0x{b:02x}' for b in invalid) or '(none)'}"
    )
    print(
        "  => 0x00 is the only byte free for a binary tag; but base59 (the '~' text form) drops a"
    )
    print(
        "     leading zero byte, so 0x00 is unusable there too. Net: no leading version byte works"
    )
    print(
        "     at all (which is why v1 has none). v2 keeps a nonzero first byte and versions out of"
    )
    print("     band. (0x02 is v1-valid, i.e. also unsafe. See strings_demo.py.)\n")


def node_entropy_report() -> None:
    print("== How uniqueness is guaranteed, and where compaction trades it away ==")
    print(
        "An id is unique by (timestamp, clock_seq, node). Three independent lines of defense:"
    )
    print(
        "  1. WITHIN a node: clock_seq is a 14-bit counter -> up to 16384 distinct ids per time"
    )
    print(
        "     bucket, and the timestamp advances between buckets. Hard guarantee below that rate."
    )
    print("  2. ACROSS nodes, EXPANDED: keeps the full 48-bit node.")
    print(
        f"       collision needs a 48-bit node birthday (~{int(math.sqrt(2 * 2**48 * math.log(2))):,} nodes for 50%)."
    )
    print("  3. ACROSS nodes, COMPACT: node is reduced to a 7-bit salt (128 values).")
    space = 2**7 * 2**14
    print(
        f"       distinct compact ids in ONE ms across all nodes = 2^7 salt * 2^14 clock = {space:,}."
    )
    for k in (100, 1000, 5000):
        p = 1 - math.exp(-(k * k) / (2 * space))
        print(
            f"       {k:>5} compact ids/ms fleet-wide -> {p:.1%} chance of a collision"
        )
    print(
        "  => Use EXPANDED for strong cross-node uniqueness; COMPACT is a size-for-uniqueness"
    )
    print(
        "     trade, safe for a single writer or a bounded fleet/rate. v1 and v2 make the SAME"
    )
    print(
        "     trade; v2's 1ms buckets are marginally safer than v1's 1.6384ms (see above).\n"
    )


def size_schedule_report() -> None:
    print("== When does the v2 compact wire jump to N bytes? (ms/2026 epoch) ==")

    def total_size(v2ms: int) -> int:
        value = (v2ms << 22) | 1  # clock=salt=0, flag=1: minimum value for this v2ms
        return 1 + (value.bit_length() + 7) // 8  # 1 length byte + payload

    def first_v2ms(target: int) -> int:
        lo, hi = 0, 1 << 62
        while lo < hi:
            mid = (lo + hi) // 2
            if total_size(mid) >= target:
                hi = mid
            else:
                lo = mid + 1
        return lo

    yr_ms = 31556952000
    for n in range(4, 13):
        ms = first_v2ms(n)
        years = ms / yr_ms
        cal = 2026 + years
        when = (
            f"year {cal:,.0f}"
            if cal > 9999
            else f"2026-01-01 + {ms / 1000:,.0f}s ({years:.2f}y) ~ {int(cal)}"
        )
        print(f"  {n:>2} bytes first at {when}")
    print(
        "  So: it climbs 4->8 bytes within the first DAY of 2026, hits 9 bytes on 2026-07-18,"
    )
    print(
        "  then stays 9 until ~2165, 10 until ~year 37,700, each jump ~256x rarer than the last."
    )
    print(
        "  (The +1 length byte over v1's 8-byte steady state is v1's VL fold, reclaimable.)\n"
    )


if __name__ == "__main__":
    collision_report()
    node_entropy_report()
    compression_report()
    print()
    tag_space_report()
    size_schedule_report()
