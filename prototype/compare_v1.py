#!/usr/bin/env python3
"""Print the SAME id encoded as v1 (current cuuid) and as v2, side by side: canonical string,
binary wire, and the "~" base59 text form. v1 rows come from the real library via
`cuuid_v2_demo --v1-vectors`; v2 rows are computed here.

Run: ./build/cuuid_v2_demo --v1-vectors | python3 prototype/compare_v1.py
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from strings_demo import (
    b59,
    v6_string,
)  # base59 + v6 canonical helper (also sets up the port path)

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python"))
import cuuid_v2 as v2  # noqa: E402


def tilde(wire_hex: str) -> str:
    return "~" + b59.encode(bytes.fromhex(wire_hex))


def main() -> None:
    for line in sys.stdin:
        greg, clock, node, v1_canon, v1_wire = line.rstrip("\n").split("\t")
        greg, clock, node = int(greg), int(clock), int(node)

        t, c, n = v2.crush(greg, clock, node)
        v2_wire = v2.encode(t, c, n).hex()
        v2_canon = v6_string(t, c, n)

        print(f"input  greg={greg} clock={clock} node=0x{node:012x}")
        print(f"  v1  canonical {v1_canon}")
        print(
            f"      wire       {v1_wire}   ({len(v1_wire) // 2} bytes)   {tilde(v1_wire)}"
        )
        print(f"  v2  canonical {v2_canon}   (UUIDv6)")
        print(
            f"      wire       {v2_wire}   ({len(v2_wire) // 2} bytes)   {tilde(v2_wire)}"
        )
        print()

    print("Differences that matter:")
    print("  - canonical layout: v1 (version 1, time scrambled -> NOT byte-sortable)")
    print("                      v2 (version 6, time first     -> byte-sortable)")
    print(
        "  - wire: both condensed, both base59-safe (nonzero first byte); v2 stays 8 bytes and"
    )
    print(
        "          drops to ~4 near the 2026 epoch, and decodes without a Mersenne Twister."
    )


if __name__ == "__main__":
    main()
