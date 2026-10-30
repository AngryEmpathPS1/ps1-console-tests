'use strict';
/*
 * hwtests/mdec-qbias/gen.js - chooses the test's blocks and writes blocks.h.
 *
 * mdec-round sends every block at q_scale 0, where a coefficient is its level
 * x 32 (this core's units) exactly and the dequantiser never runs. Here the
 * blocks carry a q_scale, so each coefficient goes through the dequantiser:
 *   DC:  ((level x qt[0]) << 4) + bias
 *   AC:  (((level x q_scale x qt[k]) >> 3) << 4) + bias
 * saturated to s15, where bias is a half step taken back towards zero: -8 for
 * a positive level and +8 for a negative one in this core (mednafen's), 5 is
 * what mdec/frame's photo fits best (the 900-pixel improvement src/psx.js
 * declines to take without a reason), and 0 is MiSTer's dequantiser. All three
 * are followed by this core's IDCT (M224: pass 1 carried, rounded once), which
 * mdec-round asks about separately.
 * The quantisation table is ps1-tests' common/mdec.cpp `quant`, which the test
 * uploads; the zigzag and the scale table's layout are the core's.
 *
 *   node hwtests/mdec-qbias/gen.js
 */
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', '..', 'src', 'psx.js'), 'utf8');
const ZZ = Uint8Array.from(src.match(/const MDEC_ZAGZIG = new Uint8Array\(\[([^\]]+)\]/)[1].split(',').map((s) => +s.trim()).filter((x) => !isNaN(x)));
const ZZT = ZZ.map((i) => ((i & 7) << 3) | (i >>> 3));
const IDCT = [23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 32138, 27245, 18204, 6392, -6393, -18205, -27246, -32139, 30273, 12539, -12540, -30274, -30274, -12540, 12539, 30273, 27245, -6393, -32139, -18205, 18204, 32138, 6392, -27246, 23170, -23171, -23171, 23170, 23170, -23171, -23171, 23170, 18204, -32139, 6392, 27245, -27246, -6393, 32138, -18205, 12539, -30274, 30273, -12540, -12540, 30273, -30274, 12539, 6392, -18205, 27245, -32139, 32138, -27246, 18204, -6393];
const QT = [0x02, 0x10, 0x10, 0x13, 0x10, 0x13, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x1a, 0x18, 0x1a, 0x1b, 0x1b, 0x1b, 0x1a, 0x1a, 0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1d, 0x1d, 0x1d, 0x22, 0x22, 0x22, 0x1d, 0x1d, 0x1d, 0x1b, 0x1b, 0x1d, 0x1d, 0x20, 0x20, 0x22, 0x22, 0x25, 0x26, 0x25, 0x23, 0x23, 0x22, 0x23, 0x26, 0x26, 0x28, 0x28, 0x28, 0x30, 0x30, 0x2e, 0x2e, 0x38, 0x38, 0x3a, 0x45, 0x45, 0x53];
const st = new Int16Array(64);
for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) st[y * 8 + x] = IDCT[x * 8 + y];
const clamp = (v) => (v < -128 ? -128 : v > 127 ? 127 : v);
const clampC = (v) => (v < -0x4000 ? -0x4000 : v > 0x3FFF ? 0x3FFF : v);

/* This core's IDCT since M224 (mdec-round's rule A). */
function idct(blk) {
  const tmp = new Float64Array(64), out = new Int16Array(64);
  for (let x = 0; x < 8; x++) for (let y = 0; y < 8; y++) { let s = 0; for (let i = 0; i < 8; i++) s += blk[x * 8 + i] * st[y * 8 + i]; tmp[y * 8 + x] = s; }
  for (let x = 0; x < 8; x++) for (let y = 0; y < 8; y++) {
    let s = 0; for (let i = 0; i < 8; i++) s += tmp[x * 8 + i] * st[y * 8 + i];
    out[x * 8 + y] = clamp(Math.floor((s + 34359738368) / 68719476736));
  }
  return out;
}
/* The dequantiser with a half-step bias of `b` (src/psx.js MDEC.decodeRLE and MDEC.bias). */
function block(dc, k, v, qs, b) {
  const bias = (x) => (x === 0 ? 0 : x < 0 ? b : -b);
  const blk = new Int16Array(64);
  blk[ZZT[0]] = qs === 0 ? dc * 32 : clampC(((dc * QT[0]) << 4) + bias(dc));
  if (k) {
    const scq = qs * QT[k];
    blk[ZZT[k]] = scq === 0 ? v * 32 : clampC((((v * scq) >> 3) << 4) + bias(v));
  }
  return blk;
}
const RULES = { B8: 8, B5: 5, B0: 0 };
const outOf = (dc, k, v, qs) => { const o = {}; for (const r in RULES) o[r] = idct(block(dc, k, v, qs, RULES[r])); return o; };
const diff = (a, b) => { let n = 0; for (let i = 0; i < 64; i++) if (a[i] !== b[i]) n++; return n; };
const sat = (o) => Object.values(o).some((a) => a.some((x) => x === 127 || x === -128));
const spread = (o) => Math.min(diff(o.B8, o.B5), diff(o.B8, o.B0), diff(o.B5, o.B0));

const pick = [];
/* Controls: the three agree. */
for (const [dc, qs] of [[0, 1], [40, 1], [-40, 1], [100, 2]]) {
  const o = outOf(dc, 0, 0, qs);
  if (!sat(o) && !diff(o.B8, o.B5) && !diff(o.B8, o.B0)) pick.push({ dc, k: 0, v: 0, qs, o, role: 'control' });
  if (pick.length === 2) break;
}
/* No DC-only rows: a DC coefficient is 32 x level less the bias, and 23170 squared falls just short of 2^29, so
 * every half-way level lands a hair below the boundary with any bias from 0 to 8 - no DC-only block tells them apart
 * (searched from -300 to 300). */
/* A DC and one AC coefficient at a q_scale: each rule against both others. */
for (const [k, qs] of [[1, 1], [2, 2], [3, 1], [5, 3], [9, 1], [14, 2], [20, 1], [27, 4], [35, 1], [44, 2]]) {
  let best = null;
  for (let dc = -60; dc <= 60; dc += 4) for (let v = -60; v <= 60; v++) {
    if (!v) continue;
    const o = outOf(dc, k, v, qs);
    if (sat(o)) continue;
    const s = spread(o);
    if (!best || s > best.s) best = { dc, k, v, qs, o, s, role: 'AC' };
  }
  if (best && best.s > 0) pick.push(best);
}

const hw = (x) => '0x' + (x & 0xFFFF).toString(16).padStart(4, '0');
const bytes = (arr) => Array.from(arr, (x) => (x + 128) & 0xFF).join(',');
const lines = ['/* Generated by hwtests/mdec-qbias/gen.js - see main.cpp. */', `#define NBLOCKS ${pick.length}`,
  'struct Block { const char* role; int16_t dc, k, v, qs; uint16_t words; uint16_t rle[4]; uint8_t b8[64], b5[64], b0[64]; };',
  'static const Block blocks[NBLOCKS] = {'];
for (const p of pick) {
  const rle = [((p.qs & 0x3F) << 10) | (p.dc & 0x3FF)];
  if (p.k) rle.push((((p.k - 1) & 0x3F) << 10) | (p.v & 0x3FF));
  rle.push(0xFE00);                                // end of block
  if (rle.length & 1) rle.push(0xFE00);            // the word's other half: no padding WORDS (STATUS M236)
  const words = rle.length / 2;
  while (rle.length < 4) rle.push(0);
  lines.push(`  { "${p.role}", ${p.dc}, ${p.k}, ${p.v}, ${p.qs}, ${words}, { ${rle.map(hw).join(', ')} },`);
  lines.push(`    { ${bytes(p.o.B8)} },`, `    { ${bytes(p.o.B5)} },`, `    { ${bytes(p.o.B0)} } },`);
}
lines.push('};');
fs.writeFileSync(path.join(__dirname, 'blocks.h'), lines.join('\n') + '\n');
for (const p of pick) console.log(`${p.role.padEnd(7)} dc ${String(p.dc).padStart(4)} AC ${String(p.k).padStart(2)} = ${String(p.v).padStart(4)} q ${p.qs}   8/5 ${diff(p.o.B8, p.o.B5)}  8/0 ${diff(p.o.B8, p.o.B0)}  5/0 ${diff(p.o.B5, p.o.B0)}`);
