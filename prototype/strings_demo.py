#!/usr/bin/env python3
"""Show how a cuuid id looks in every form: fields, canonical UUIDv6 string, the binary
condensed wire, and Xapiand's '~' base59 text encoding. Also demonstrates concretely why a
leading 0x00 tag is destroyed by the '~' encoding (base59 drops leading zero bytes).

Run: python3 strings_demo.py
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python"))
import cuuid_v2 as v2  # noqa: E402


# --- Xapiand's base59, ported verbatim from contrib/python/cuuid/base_x.py (b59) ---
class BaseX:
    def __init__(self, alphabet: str, translate: str) -> None:
        self.alphabet = alphabet
        self.base = len(alphabet)
        self.decoder = [self.base] * 256
        for i, a in enumerate(alphabet):
            self.decoder[ord(a)] = i
        x = -1
        for a in translate:
            i = self.decoder[ord(a)]
            self.decoder[ord(a)] = i if i < self.base else x

    def _encode_int(self, i: int) -> tuple[str, int]:
        s, chk = "", 0
        while i:
            i, idx = divmod(i, self.base)
            s = self.alphabet[idx] + s
            chk += idx
        chk += len(s) + len(s) // self.base
        return s, chk % self.base

    def encode(self, v: bytes) -> str:
        acc, p = 0, 1
        for c in reversed(
            bytearray(v)
        ):  # big-endian bytes -> integer (leading zeros vanish here)
            acc += p * c
            p <<= 8
        result, chk = self._encode_int(acc)
        chk = (self.base - (chk % self.base)) % self.base
        return result + self.alphabet[chk]

    def decode(self, v: str) -> bytes:
        acc = 0
        for ch in v[:-1]:  # last char is the checksum
            i = self.decoder[ord(ch)]
            if i < self.base:
                acc = acc * self.base + i
        out = []
        while acc:
            out.append(acc & 0xFF)
            acc >>= 8
        return bytes(reversed(out))


b59 = BaseX("zGLUAC2EwdDRrkWBatmscxyYlg6jhP7K53TibenZpMVuvoO9H4XSQq8FfJN", "~l1IO0")


def v6_string(time: int, clock: int, node: int) -> str:
    b = v2.to_v6_bytes(time, clock, node)
    h = b.hex()
    return f"{h[0:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"


def show(label: str, time: int, clock: int, node: int) -> None:
    t, c, n = v2.crush(time, clock, node)  # compact form
    wire = v2.encode(t, c, n)
    tilde = "~" + b59.encode(wire)
    print(f"--- {label} ---")
    print(f"  fields         time={t} clock={c} node=0x{n:012x}")
    print(f"  canonical v6   {v6_string(t, c, n)}")
    print(f"  binary wire    {wire.hex()}   ({len(wire)} bytes)")
    print(
        f"     length byte 0x{wire[0]:02x} ({wire[0]})   <- first byte, nonzero => base59-safe"
    )
    print(f"     payload     {wire[1:].hex()}")
    print(f"  '~' base59     {tilde}")
    # base59 round-trips because the first byte is nonzero:
    assert b59.decode(tilde[1:]) == wire, "base59 round-trip failed"
    print()


def main() -> None:
    yr = 31556952000
    base_ms = v2.EPOCH_2026_MS + 190 * 86400 * 1000  # mid-2026
    for label, off_ms, clock, node in [
        ("A (2026)", 0, 0x1ABC, 0x0123456789AB),
        ("B (2026, +1ms)", 1, 0x1ABC, 0x0123456789AB),
        ("C (2035)", 9 * yr, 0x0042, 0xFEDCBA987654),
    ]:
        greg = v2.GREG_EPOCH_100NS + (base_ms + off_ms) * 10000
        show(label, greg, clock, node)

    print("=== Why v2 has NO leading tag byte (base59 would drop it) ===")
    greg = v2.GREG_EPOCH_100NS + base_ms * 10000
    t, c, n = v2.crush(greg, 0x1ABC, 0x0123456789AB)
    real = v2.encode(t, c, n)  # the real wire: first byte nonzero
    hypo = b"\x00" + real  # if we HAD prepended a 0x00 tag for dispatch
    print(
        f"  real v2 wire (no tag) : {real.hex()}   first byte 0x{real[0]:02x} (nonzero)"
    )
    print(
        f"    base59 round-trip   : {'survives' if b59.decode(b59.encode(real)) == real else 'FAILS'}"
    )
    print(f"  hypothetical 0x00 tag : {hypo.hex()}")
    dec = b59.decode(b59.encode(hypo))
    print(f"    base59 round-trip   : {hypo.hex()} -> {dec.hex()}")
    print(
        f"    leading 0x00 kept?  : {'yes' if dec == hypo else 'NO -- tag byte lost'}"
    )
    print(
        "  base59 folds the bytes into one big integer, so a leading 0x00 contributes nothing and"
    )
    print(
        "  cannot be recovered. That is why v1 guarantees a nonzero first byte and keeps its"
    )
    print(
        "  compact/expanded flags in the TRAILING bytes, and why v2 does the same: no leading tag."
    )


if __name__ == "__main__":
    main()
