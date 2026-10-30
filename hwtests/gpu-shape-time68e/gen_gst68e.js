'use strict';
// Builds hwtests/gpu-shape-time68e/main.cpp from gpu-shape-time68c's: three shapes, 320x240 only, for many runs.
// node hwtests/gpu-shape-time68e/gen_gst68e.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time68c', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time68e - THE RUN-TO-RUN STATE: gpu-shape-time68c's three most varying shapes alone, VRAM cleared first,
 * 320x240 only, for many runs (2026-10-26).
 *
 * WHY. gpu-shape-time68 / 68c on the SCPH-7501: copies and sprites differ from run to run at the same start. Clearing VRAM
 * first takes 320x240's spread to a tenth but not to nothing (the cleared runs still differ on 14-47 of 96 cells), and two
 * uncleared runs of six were identical to the cycle - a few discrete start states (the GPU clock's phase against the CPU's?)
 * rather than noise. Many short runs of the same shapes: do they fall into a few identical groups, and how many?
 *
 * WHAT IT DOES. As gpu-shape-time68c (all VRAM filled first; 32 starts 67 cycles apart from 200,000 cycles after VBlank),
 * under 320x240 15-bit only: a VRAM copy 64x64, a 4-bit sprite 256x16, a copy 256x32.
 *
 * READ IT LIKE THIS. The runs against each other. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time68e --ntsc --frames=40000 --quiet=40000\`.
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    copyS("copy 64x64", 64, 64); sprite("spr4 256x16", 0x65u, 0, 256, 16); copyS("copy 256x32", 256, 32);
}
`);
rep('static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };', 'static const char* const MN[1] = { "320x240 15-bit" };');
rep('static const uint32_t MODE[2] = { 0, 0x01 };', 'static const uint32_t MODE[1] = { 0x01 };');
rep('static uint32_t base[2][20], r26[2][20][32];', 'static uint32_t base[1][20], r26[1][20][32];');
rep('if (m == 1) {   /* mode 0 is Unirom\'s own, left as it is */', '{');
s = s.split('for (int m = 0; m < 2; m++)').join('for (int m = 0; m < 1; m++)');
s = s.split('gpu/gpu-shape-time68c').join('gpu/gpu-shape-time68e');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), fs.readFileSync(path.join(R, 'gpu-shape-time68c', 'Makefile'), 'utf8').split('gpu-shape-time68c').join('gpu-shape-time68e'));
console.log('written');
