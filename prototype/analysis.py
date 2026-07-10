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


if __name__ == "__main__":
    collision_report()
    compression_report()
