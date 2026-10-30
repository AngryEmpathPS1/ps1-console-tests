'use strict';
// Builds hwtests/gpu-shape-time63/main.cpp from gpu-shape-time60's: a new header, new shapes (poly-lines' later segments by
// kind, 1-4 segments). node hwtests/gpu-shape-time63/gen_gst63.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time63 - A POLY-LINE'S LATER SEGMENTS BY KIND: tilted, level and steep, 1-4 segments (2026-10-25 night).
 *
 * WHY. v0.377.188-189 (src/psx.js GPU_LINE2.segment, 18 ticks) charge every later segment of a poly-line its own line less
 * ~23 cycles, fitted on 64-wide segments rising 0-2 (gpu-shape-time60) and confirmed on diagonal and steep ones (61, 62). But
 * gpu-shape-time62's 2-segment TILTED poly-lines (64 wide, rises 8 / 16 / 32) read 6-7 cycles dearer than that, its level
 * ones (128 / 255 wide) 3 cheaper - from one second segment each. Is it the second segment only (gst60's third segments
 * matched where its second did not), the rise, or the width? And where between a tilted and a steep segment does the next
 * vertex start to be taken early (steep: 6-50 cycles before the end; tilted and level: within 4)?
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat poly-lines (48h, zigzag down dy and back, ended by 55555555h) from (192,128) of 1-4
 * segments: 64 wide rising 4, 8, 16, 32 and 48; 128 wide rising 8; 32 wide rising 16; level 128 wide; steep 16 wide 32 down.
 * A poly's words before the last four written untimed, so N segments less N - 1 is one segment.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time63 --ntsc --frames=60000 --quiet=60000\`.
 */
`);
const fams = [['tilt', 64, 4], ['tilt', 64, 8], ['tilt', 64, 16], ['tilt', 64, 32], ['tilt', 64, 48], ['tilt', 128, 8],
  ['tilt', 32, 16], ['level', 128, 0], ['steep', 16, 32]];
const defs = fams.map(([k, w, dy]) => [1, 2, 3, 4].map((n) => `    polyLine("${k} ${n}x${w},${dy}", 0x48, ${n}, ${w}, ${dy});`).join('\n')).join('\n');
rep(/\/\* a poly-line from \(192,128\) of n segments 64 wide[\s\S]*?\nstatic void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h (48h flat) */
static void polyLine(const char* nm, int c, int n, int w, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    for (int i = 1; i <= n; i++) W(s, Q(192, w * i, (i & 1) ? dy : 0));
    W(s, 0x55555555u);
}
static void defs() {
${defs}
}
`);
s = s.split('gpu/gpu-shape-time60').join('gpu/gpu-shape-time63');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'Makefile'), 'utf8').split('gpu-shape-time60').join('gpu-shape-time63');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines', fams.length * 4, 'shapes');
