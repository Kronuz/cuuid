// cuuid format-v2, JavaScript port (PROTOTYPE).
//
// Byte-identical to the C++ prototype (prototype/cuuid_v2.hh) with NO Mersenne Twister.
// splitmix64 is fixed-width integer arithmetic (here in BigInt, masked to 64 bits), so it
// is identical across C++, Python, and JavaScript by construction. That is exactly the
// cross-system determinism the v1 design had to fake by embedding a hand-rolled MT.
//
// Run `node cuuid_v2.mjs <vectors.tsv>` to validate against the C++ test vectors.

import { readFileSync } from "node:fs";

const MASK64 = (1n << 64n) - 1n;
const GREG_EPOCH_100NS = 0x01b21dd213814000n; // 1582-10-15 .. 1970, in 100ns
const EPOCH_2026_MS = 1767225600000n;         // 2026-01-01T00:00:00Z, unix ms

const CLOCK_MASK = (1n << 14n) - 1n;
const NODE_MASK = (1n << 48n) - 1n;
const SALT_MASK = (1n << 7n) - 1n;
const MULTICAST = 0x010000000000n;
const V2_TAG = 0x02;

function splitmix64(x) {
  x = (x + 0x9e3779b97f4a7c15n) & MASK64;
  x = ((x ^ (x >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  x = ((x ^ (x >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  return x ^ (x >> 31n);
}

function gregToV2ms(greg) {
  return (greg - GREG_EPOCH_100NS) / 10000n - EPOCH_2026_MS;
}
function v2msToGreg(v2ms) {
  return (v2ms + EPOCH_2026_MS) * 10000n + GREG_EPOCH_100NS;
}

function reconstructNode(v2ms, clock, salt) {
  const seed = splitmix64((v2ms ^ (clock << 44n) ^ (salt << 57n)) & MASK64);
  let node = splitmix64(seed);
  node &= NODE_MASK & ~SALT_MASK;
  node |= salt;
  node |= MULTICAST;
  return node;
}

function toV6Bytes(time, clock, node) {
  const th = (time >> 28n) & 0xffffffffn;
  const tm = (time >> 12n) & 0xffffn;
  const tl = time & 0x0fffn;
  const b = new Uint8Array(16);
  b[0] = Number((th >> 24n) & 0xffn);
  b[1] = Number((th >> 16n) & 0xffn);
  b[2] = Number((th >> 8n) & 0xffn);
  b[3] = Number(th & 0xffn);
  b[4] = Number((tm >> 8n) & 0xffn);
  b[5] = Number(tm & 0xffn);
  b[6] = Number(0x60n | (tl >> 8n));
  b[7] = Number(tl & 0xffn);
  b[8] = Number(0x80n | ((clock >> 8n) & 0x3fn));
  b[9] = Number(clock & 0xffn);
  for (let i = 0; i < 6; i++) b[10 + i] = Number((node >> BigInt((5 - i) * 8)) & 0xffn);
  return b;
}

function big16be(v) {
  const b = new Uint8Array(16);
  for (let i = 15; i >= 0; i--) {
    b[i] = Number(v & 0xffn);
    v >>= 8n;
  }
  return b;
}

function encode(time, clock, node) {
  const salt = node & SALT_MASK;
  const v2ms = gregToV2ms(time);
  const compact = node === reconstructNode(v2ms, clock, salt);
  let v;
  if (compact) {
    v = (v2ms << 22n) | ((clock & CLOCK_MASK) << 8n) | (salt << 1n) | 1n;
  } else {
    v = (v2ms << 63n) | ((clock & CLOCK_MASK) << 49n) | ((node & NODE_MASK) << 1n);
  }
  const buf = big16be(v);
  let start = 0;
  while (start < 15 && buf[start] === 0) start++;
  const body = buf.slice(start);
  const out = new Uint8Array(2 + body.length);
  out[0] = V2_TAG;
  out[1] = body.length;
  out.set(body, 2);
  return out;
}

function decode(wire) {
  if (wire.length === 0 || wire[0] !== V2_TAG) throw new Error("v2: not a v2 id");
  const length = wire[1];
  let v = 0n;
  for (let i = 0; i < length; i++) v = (v << 8n) | BigInt(wire[2 + i]);
  if (v & 1n) {
    const salt = (v >> 1n) & SALT_MASK;
    const clock = (v >> 8n) & CLOCK_MASK;
    const v2ms = v >> 22n;
    return [v2msToGreg(v2ms), clock, reconstructNode(v2ms, clock, salt)];
  }
  const node = (v >> 1n) & NODE_MASK;
  const clock = (v >> 49n) & CLOCK_MASK;
  const v2ms = v >> 63n;
  return [v2msToGreg(v2ms), clock, node];
}

function toHex(u8) {
  return Array.from(u8, (x) => x.toString(16).padStart(2, "0")).join("");
}
function fromHex(h) {
  const u8 = new Uint8Array(h.length / 2);
  for (let i = 0; i < u8.length; i++) u8[i] = parseInt(h.substr(i * 2, 2), 16);
  return u8;
}

function validate(path) {
  const lines = readFileSync(path, "ascii").split("\n").filter((l) => l.length);
  let ok = 0;
  for (const [i, line] of lines.entries()) {
    const [, t, c, n, wireHex, v6Hex] = line.split("\t");
    const time = BigInt(t), clock = BigInt(c), node = BigInt(n);
    const [dt, dc, dn] = decode(fromHex(wireHex));
    if (dt !== time || dc !== clock || dn !== node) throw new Error(`decode mismatch line ${i + 1}`);
    if (toHex(encode(time, clock, node)) !== wireHex) throw new Error(`encode mismatch line ${i + 1}`);
    if (toHex(toV6Bytes(time, clock, node)) !== v6Hex) throw new Error(`v6 mismatch line ${i + 1}`);
    ok++;
  }
  return ok;
}

const path = process.argv[2];
if (!path) {
  console.error("usage: node cuuid_v2.mjs <vectors.tsv>");
  process.exit(2);
}
console.log(`JavaScript port: ${validate(path)} vectors match C++ byte-for-byte [OK]`);
