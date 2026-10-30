'use strict';
// Builds hwtests/gpu-shape-time61/main.cpp from gpu-shape-time60's: a new header, new shapes (poly-lines steep, gouraud and
// semi; the shallow rises v0.377.188 assumed). node hwtests/gpu-shape-time61/gen_gst61.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time61 - POLY-LINES BEYOND THE FLAT SHALLOW ONES, and the shallow rises v0.377.188 assumed (2026-10-25 night).
 *
 * WHY. gpu-shape-time60 on the SCPH-7501 measured flat shallow lines (rises 0-8, 16-255 pixels) and flat poly-lines of 64-wide
 * segments rising 0-2 rows: a shallow line's per-pixel rate falls with its rise (1.513 cycles a pixel at a rise of 1 ... 1.953
 * from 8), and each later segment of a poly-line costs its own line less ~23 cycles - the line's set-up (40) less ~11 of its
 * own. v0.377.188 (src/psx.js GPU_LINE2.shallow / .segment) took both, and ASSUMED: rises 5 and 7 (6 at 64 pixels only, 3 at
 * 64 only), lines under 16 pixels never dearer than the formula, a steep or diagonal segment's set-up the shallow one's, a
 * gouraud segment paying its whole 98-tick gouraud set-up each, a semi one nothing more.
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat lines (40h) from (192,128) by (dx,dy): 4,1; 8,1; 8,2; 8,3; 64,5; 64,7; 128,3; 128,5;
 * 128,6; 128,7; 255,3; 255,8. Gouraud (50h) 128,1 and 128,2; semi (42h) 128,1, 128,2 and 255,1. Poly-lines of 1-4 segments
 * from (192,128), zigzag (down dy, back up): flat 48h DIAGONAL (64 wide, 64 down) and STEEP (16 wide, 64 down); gouraud 58h
 * level and 1 row (64 wide; the colours alternate); semi 4Ah level and 1 row (64 wide). A poly's words before the last four
 * written untimed, so N segments less N - 1 is one segment.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time61 --ntsc --frames=60000 --quiet=60000\`.
 */
`);
rep(/\/\* a poly-line from \(192,128\) of n segments 64 wide[\s\S]*?\nstatic void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h: c 48h flat, 4Ah semi,
   58h gouraud (the colours alternate 204080h / 408020h) */
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
    line11("4,1", 0x40, 4, 1);
    line11("8,1", 0x40, 8, 1);
    line11("8,2", 0x40, 8, 2);
    line11("8,3", 0x40, 8, 3);
    line11("64,5", 0x40, 64, 5);
    line11("64,7", 0x40, 64, 7);
    line11("128,3", 0x40, 128, 3);
    line11("128,5", 0x40, 128, 5);
    line11("128,6", 0x40, 128, 6);
    line11("128,7", 0x40, 128, 7);
    line11("255,3", 0x40, 255, 3);
    line11("255,8", 0x40, 255, 8);
    line11("gour 128,1", 0x50, 128, 1);
    line11("gour 128,2", 0x50, 128, 2);
    line11("semi 128,1", 0x42, 128, 1);
    line11("semi 128,2", 0x42, 128, 2);
    line11("semi 255,1", 0x42, 255, 1);
    polyLine("diag 1x64,64", 0x48, 1, 64, 64);
    polyLine("diag 2x64,64", 0x48, 2, 64, 64);
    polyLine("diag 3x64,64", 0x48, 3, 64, 64);
    polyLine("diag 4x64,64", 0x48, 4, 64, 64);
    polyLine("steep 1x16,64", 0x48, 1, 16, 64);
    polyLine("steep 2x16,64", 0x48, 2, 16, 64);
    polyLine("steep 3x16,64", 0x48, 3, 16, 64);
    polyLine("steep 4x16,64", 0x48, 4, 16, 64);
    polyLine("gpoly 1x64,0", 0x58, 1, 64, 0);
    polyLine("gpoly 2x64,0", 0x58, 2, 64, 0);
    polyLine("gpoly 3x64,0", 0x58, 3, 64, 0);
    polyLine("gpoly 4x64,0", 0x58, 4, 64, 0);
    polyLine("gpoly 1x64,1", 0x58, 1, 64, 1);
    polyLine("gpoly 2x64,1", 0x58, 2, 64, 1);
    polyLine("gpoly 3x64,1", 0x58, 3, 64, 1);
    polyLine("gpoly 4x64,1", 0x58, 4, 64, 1);
    polyLine("spoly 1x64,0", 0x4A, 1, 64, 0);
    polyLine("spoly 2x64,0", 0x4A, 2, 64, 0);
    polyLine("spoly 3x64,0", 0x4A, 3, 64, 0);
    polyLine("spoly 4x64,0", 0x4A, 4, 64, 0);
    polyLine("spoly 1x64,1", 0x4A, 1, 64, 1);
    polyLine("spoly 2x64,1", 0x4A, 2, 64, 1);
    polyLine("spoly 3x64,1", 0x4A, 3, 64, 1);
    polyLine("spoly 4x64,1", 0x4A, 4, 64, 1);
}
`);
s = s.split('gpu/gpu-shape-time60').join('gpu/gpu-shape-time61');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time60', 'Makefile'), 'utf8').split('gpu-shape-time60').join('gpu-shape-time61');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
