#!/usr/bin/env python3
"""cuuid format-v2, Python port (PROTOTYPE).

The whole point of this file: it produces byte-identical wires to the C++ prototype
(prototype/cuuid_v2.hh) with NO Mersenne Twister. The v1 format forced the Python port
to embed a hand-rolled MT (contrib/python/cuuid/mertwis.py) purely to match C++'s node
bytes across systems. splitmix64 is fixed-width integer arithmetic, so it is identical
everywhere by construction, and this whole port is ~40 lines.

Run `python3 cuuid_v2.py <vectors.tsv>` to validate against the C++ test vectors.
"""

from __future__ import annotations

import sys

MASK64 = (1 << 64) - 1
GREG_EPOCH_100NS = 0x01B21DD213814000  # 1582-10-15 .. 1970, in 100ns
EPOCH_2026_MS = 1767225600000  # 2026-01-01T00:00:00Z, unix ms

CLOCK_BITS, NODE_BITS, SALT_BITS = 14, 48, 7
CLOCK_MASK = (1 << CLOCK_BITS) - 1
NODE_MASK = (1 << NODE_BITS) - 1
SALT_MASK = (1 << SALT_BITS) - 1
MULTICAST = 0x010000000000
V2_TAG = 0x02


def splitmix64(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & MASK64
    return x ^ (x >> 31)


def greg_to_v2ms(greg: int) -> int:
    return (greg - GREG_EPOCH_100NS) // 10000 - EPOCH_2026_MS


def v2ms_to_greg(v2ms: int) -> int:
    return (v2ms + EPOCH_2026_MS) * 10000 + GREG_EPOCH_100NS


def reconstruct_node(v2ms: int, clock: int, salt: int) -> int:
    seed = splitmix64((v2ms ^ (clock << 44) ^ (salt << 57)) & MASK64)
    node = splitmix64(seed)
    node &= NODE_MASK & ~SALT_MASK
    node |= salt
    node |= MULTICAST
    return node


def crush(time: int, clock: int, node: int) -> tuple[int, int, int]:
    salt = node & SALT_MASK
    v2ms = greg_to_v2ms(time)
    return (v2ms_to_greg(v2ms), clock, reconstruct_node(v2ms, clock, salt))


def to_v6_bytes(time: int, clock: int, node: int) -> bytes:
    th = (time >> 28) & 0xFFFFFFFF
    tm = (time >> 12) & 0xFFFF
    tl = time & 0x0FFF
    b = bytearray(16)
    b[0], b[1], b[2], b[3] = (
        (th >> 24) & 0xFF,
        (th >> 16) & 0xFF,
        (th >> 8) & 0xFF,
        th & 0xFF,
    )
    b[4], b[5] = (tm >> 8) & 0xFF, tm & 0xFF
    b[6], b[7] = 0x60 | (tl >> 8), tl & 0xFF
    b[8], b[9] = 0x80 | ((clock >> 8) & 0x3F), clock & 0xFF
    for i in range(6):
        b[10 + i] = (node >> ((5 - i) * 8)) & 0xFF
    return bytes(b)


def encode(time: int, clock: int, node: int) -> bytes:
    salt = node & SALT_MASK
    v2ms = greg_to_v2ms(time)
    compact = node == reconstruct_node(v2ms, clock, salt)
    if compact:
        v = (v2ms << 22) | ((clock & CLOCK_MASK) << 8) | (salt << 1) | 1
    else:
        v = (v2ms << 63) | ((clock & CLOCK_MASK) << 49) | ((node & NODE_MASK) << 1)
    buf = v.to_bytes(16, "big")
    start = 0
    while start < 15 and buf[start] == 0:
        start += 1
    body = buf[start:]
    return bytes([V2_TAG, len(body)]) + body


def decode(wire: bytes) -> tuple[int, int, int]:
    if not wire or wire[0] != V2_TAG:
        raise ValueError("v2: not a v2 id")
    length = wire[1]
    v = int.from_bytes(wire[2 : 2 + length], "big")
    if v & 1:  # compact
        salt = (v >> 1) & SALT_MASK
        clock = (v >> 8) & CLOCK_MASK
        v2ms = v >> 22
        return (v2ms_to_greg(v2ms), clock, reconstruct_node(v2ms, clock, salt))
    node = (v >> 1) & NODE_MASK
    clock = (v >> 49) & CLOCK_MASK
    v2ms = v >> 63
    return (v2ms_to_greg(v2ms), clock, node)


def _validate(path: str) -> int:
    ok = 0
    with open(path, encoding="ascii") as fh:
        for lineno, line in enumerate(fh, 1):
            mode, t, c, n, wire_hex, v6_hex = line.rstrip("\n").split("\t")
            time, clock, node = int(t), int(c), int(n)
            wire = bytes.fromhex(wire_hex)
            assert decode(wire) == (time, clock, node), f"decode mismatch line {lineno}"
            assert encode(time, clock, node).hex() == wire_hex, (
                f"encode mismatch line {lineno}"
            )
            assert to_v6_bytes(time, clock, node).hex() == v6_hex, (
                f"v6 mismatch line {lineno}"
            )
            ok += 1
    return ok


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: cuuid_v2.py <vectors.tsv>", file=sys.stderr)
        sys.exit(2)
    count = _validate(sys.argv[1])
    print(f"Python port: {count} vectors match C++ byte-for-byte [OK]")
