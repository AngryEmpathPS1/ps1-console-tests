'use strict';
// Builds hwtests/gpu-shape-time62/main.cpp from gpu-shape-time60's: a new header, a per-shape GP0(E6h), new shapes (when a
// poly-line takes its next vertex; semi and mask-checked lines steep and level). node hwtests/gpu-shape-time62/gen_gst62.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time62 - WHEN A POLY-LINE TAKES ITS NEXT VERTEX, and semi and mask-checked lines off the level (2026-10-25 night).
 *
 * WHY. gpu-shape-time61 on the SCPH-7501: a poly-line's later vertex is taken (GPUSTAT bit 28) 24-47 cycles before a STEEP
 * segment ends (diagonal 64x64: 286 / 557 / 807 against its ends 316 / 606 / 897) but within 0-4 of a SHALLOW one's end - and
 * this core takes it at the end (src/psx.js GPU_LINE2's KNOWN-OPEN, v0.377.189). Is it a number of rows left, a time, or the
 * rise? And v0.377.189's semi set-up (~4.6 cycles once a command) was measured on level and shallow lines only; this core
 * gives a mask-checked line (GP0(E6h) bit 1) the semi line's costs, unmeasured.
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking), with GP0(E6h) set per shape. Flat lines (40h) from (192,128) by (dx,dy): 16,64; 32,32;
 * 8,128 - and the same semi (42h); mask-checked (E6h = 2) flat lines 64,0; 128,1; 16,64; a gouraud semi line (52h) 128,1.
 * Poly-lines (48h, zigzag down dy and back, ended by 55555555h) of 1 and 2 segments: diagonal 16x16, 32x32, 128x128; steep
 * 8x64, 8x128; tilted 64x8, 64x16, 64x32; level 128 and 255 wide; and a gouraud (58h) diagonal 64x64. The 2-segment
 * poly's bit-28 D against the 1-segment poly's bit-26 D is how early the next vertex is taken.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time62 --ntsc --frames=60000 --quiet=60000\`.
 */
`);
rep('struct Shape { const char* name; int depth; int n; uint32_t w[12]; };', 'struct Shape { const char* name; int depth; uint32_t mask; int n; uint32_t w[12]; };');
rep('Shape& s = S[NS++]; s.name = name; s.depth = depth; s.n = 0; return s; }', 'Shape& s = S[NS++]; s.name = name; s.depth = depth; s.mask = 0; s.n = 0; return s; }');
rep(/\/\* a poly-line from \(192,128\) of n segments 64 wide[\s\S]*?\nstatic void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* line11 with GP0(E6h) = mask while it is timed */
static void lineM(const char* nm, int c, int dx, int dy, uint32_t mask) { line11(nm, c, dx, dy); S[NS - 1].mask = mask; }
/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h: c 48h flat, 58h
   gouraud (the colours alternate 204080h / 408020h) */
static void polyLine(const char* nm, int c, int n, int w, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    for (int i = 1; i <= n; i++) {
        if (c & 0x10) W(s, (i & 1) ? G2 : F);
        W(s, Q(192, w * i, (i & 1) ? dy : 0));
    }
    W(s, 0x55555555u);
}
static void defs() {
    line11("16,64", 0x40, 16, 64);
    line11("32,32", 0x40, 32, 32);
    line11("8,128", 0x40, 8, 128);
    line11("semi 16,64", 0x42, 16, 64);
    line11("semi 32,32", 0x42, 32, 32);
    line11("semi 8,128", 0x42, 8, 128);
    lineM("mask 64,0", 0x40, 64, 0, 2);
    lineM("mask 128,1", 0x40, 128, 1, 2);
    lineM("mask 16,64", 0x40, 16, 64, 2);
    line11("gsemi 128,1", 0x52, 128, 1);
    polyLine("diag 1x16,16", 0x48, 1, 16, 16);
    polyLine("diag 2x16,16", 0x48, 2, 16, 16);
    polyLine("diag 1x32,32", 0x48, 1, 32, 32);
    polyLine("diag 2x32,32", 0x48, 2, 32, 32);
    polyLine("diag 1x128,128", 0x48, 1, 128, 128);
    polyLine("diag 2x128,128", 0x48, 2, 128, 128);
    polyLine("steep 1x8,64", 0x48, 1, 8, 64);
    polyLine("steep 2x8,64", 0x48, 2, 8, 64);
    polyLine("steep 1x8,128", 0x48, 1, 8, 128);
    polyLine("steep 2x8,128", 0x48, 2, 8, 128);
    polyLine("tilt 1x64,8", 0x48, 1, 64, 8);
    polyLine("tilt 2x64,8", 0x48, 2, 64, 8);
    polyLine("tilt 1x64,16", 0x48, 1, 64, 16);
    polyLine("tilt 2x64,16", 0x48, 2, 64, 16);
    polyLine("tilt 1x64,32", 0x48, 1, 64, 32);
    polyLine("tilt 2x64,32", 0x48, 2, 64, 32);
    polyLine("level 1x128", 0x48, 1, 128, 0);
    polyLine("level 2x128", 0x48, 2, 128, 0);
    polyLine("level 1x255", 0x48, 1, 255, 0);
    polyLine("level 2x255", 0x48, 2, 255, 0);
    polyLine("gdiag 1x64,64", 0x58, 1, 64, 64);
    polyLine("gdiag 2x64,64", 0x58, 2, 64, 64);
}
`);
rep('waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();',
    'waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); write32(GP0, 0xE6000000u | S[i].mask); waitReady();');
rep('    write32(GP0, 0xE1000400u | tpage(2));\n    write16(T2_MODE, m2);', '    write32(GP0, 0xE1000400u | tpage(2));\n    write32(GP0, 0xE6000000u);\n    write16(T2_MODE, m2);');
s = s.split('gpu/gpu-shape-time60').join('gpu/gpu-shape-time62');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'Makefile'), 'utf8').split('gpu-shape-time60').join('gpu-shape-time62');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
