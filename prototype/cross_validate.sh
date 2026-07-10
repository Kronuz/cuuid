#!/usr/bin/env bash
# Cross-language validation of the cuuid v2 prototype: build the C++ codec, have it emit
# test vectors, then check the Python and JavaScript ports reproduce every wire byte-for-byte.
# The point: splitmix64 makes all three agree with NO Mersenne Twister anywhere.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --target cuuid_v2_demo >/dev/null

VEC="$(mktemp -t v2vec.XXXXXX)"
trap 'rm -f "$VEC"' EXIT
./build/cuuid_v2_demo --vectors > "$VEC"
echo "C++ emitted $(wc -l < "$VEC" | tr -d ' ') test vectors"

PY="${PYTHON:-python3}"
"$PY" prototype/python/cuuid_v2.py "$VEC"
node prototype/javascript/cuuid_v2.mjs "$VEC"
echo "cross-language: C++ == Python == JavaScript [OK]"
