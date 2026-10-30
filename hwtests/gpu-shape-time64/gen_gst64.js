'use strict';
// Builds hwtests/gpu-shape-time64/main.cpp from gpu-shape-time60's: a new header, new shapes (short level and tilted lines;
// steep poly-lines by run length). node hwtests/gpu-shape-time64/gen_gst64.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time64 - SHORT LINES, and a steep poly-line's early vertex by its run length (2026-10-25 night).
 *
 * WHY. Two KNOWN-OPENs of src/psx.js GPU_LINE2. (1) A short level line: gpu-shape-time3's 4-pixel one reads 33 cycles where
 * this core has 39, gst60's 17-pixel one 49 / 52 - the level fit (34 + 1.0 a pixel) is from 32-255 pixels. (2) A steep
 * segment's next vertex is taken before it ends (gst61-63, the SCPH-7501): diagonals 6 / 14 / 30 / 48 cycles early at 17 /
 * 33 / 65 / 129 rows, 16 x 32 30 and then 49, 8 x 64 45, 8 x 128 50 - read as a write backlog growing ~0.455 a row on a
 * diagonal and ~0.9 where x steps less often, capped near 49 and carried into the next segment. How does it grow with the
 * x run, and does a vertical segment build one?
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat level lines (40h) from (192,128) of 1-8, 10, 12, 14, 16, 20, 24 and 28 pixels; flat
 * lines rising 1 row 3, 4, 5, 7, 9 and 13 pixels across; gouraud level lines (50h) of 4 and 16 pixels. Flat poly-lines (48h,
 * zigzag across dx and back, 32 down a segment, ended by 55555555h) of 1 and 2 segments at dx 32, 16, 11, 8, 4, 1 and 0
 * (x every 1, 2, 3, 4, 8 and 32 rows, and upright). The 2-segment poly's bit-28 D against the 1-segment poly's bit-26 D is
 * how early the next vertex is taken.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time64 --ntsc --frames=60000 --quiet=60000\`.
 */
`);
const lv = [1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28].map((p) => `    line11("level ${p}", 0x40, ${p - 1}, 0);`);
const t1 = [3, 4, 5, 7, 9, 13].map((p) => `    line11("rise1 ${p}", 0x40, ${p - 1}, 1);`);
const gl = [4, 16].map((p) => `    line11("gour level ${p}", 0x50, ${p - 1}, 0);`);
const st = [32, 16, 11, 8, 4, 1, 0].map((dx) => [1, 2].map((n) => `    polySteep("steep ${n}x${dx},32", ${n}, ${dx});`).join('\n'));
rep(/\/\* a poly-line from \(192,128\) of n segments 64 wide[\s\S]*?\nstatic void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* a flat poly-line from (192,128) of n segments 32 down, alternately dx across and back (48h, ended by 55555555h) */
static void polySteep(const char* nm, int n, int dx) {
    Shape& s = add(nm, 2);
    W(s, 0x48000000u | F);
    for (int i = 0; i <= n; i++) W(s, Q(192, (i & 1) ? dx : 0, 32 * i));
    W(s, 0x55555555u);
}
static void defs() {
${[...lv, ...t1, ...gl, ...st].join('\n')}
}
`);
s = s.split('gpu/gpu-shape-time60').join('gpu/gpu-shape-time64');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'Makefile'), 'utf8').split('gpu-shape-time60').join('gpu-shape-time64');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines', lv.length + t1.length + gl.length + 14, 'shapes');
