'use strict';
// Builds hwtests/gpu-shape-time70/main.cpp from gpu-shape-time69's: the same exact starts (Timer 0's reset, 540 starts 4
// cycles apart), on the TALL and LONG copies whose tolls the one-row kernels do not compose into. node hwtests/gpu-shape-time70/gen_gst70.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time69', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time70 - TALL AND LONG COPIES BY WHERE THEY START, to the cycle: gpu-shape-time69's method (540 starts 4
 * cycles apart from Timer 0's reset, in 512x240 and 320x240 15-bit) on copies of 16x16, 64x16, 64x64, 256x8, 256x32 and
 * 128x16 (2026-10-26).
 *
 * WHY. gpu-shape-time69 on the SCPH-7501 measured what ONE scan-out burst costs a copy by where it lands in a 128-pixel
 * block (or a narrower row): a set-up the burst waits out, a staircase, a free window, a ramp to the whole burst at the
 * block's end - the same in both modes and in every row of a 128x4 copy - and the line-start fetch as three refill-like
 * bursts (scratch/copysim.js: gst69 RMS 26.9 -> 6.5 cycles). Applied burst by burst to gpu-shape-time68's tall copies it
 * over-charges them badly (320x240's 256x32 356 cycles against the console's 152, 64x64 347 against 298; 512x240's 256x32
 * 679 against 538): over many lines the fetch costs a long copy far less than it costs a one-row copy - as though the
 * fetch, which only has to fill the FIFO before the next line's display, waits for the copy's idle stretch. gpu-shape-time68's
 * 32 starts, +-30 cycles each, cannot show how: these copies at gst69's exact starts can.
 *
 * WHAT IT DOES. As gpu-shape-time69 (VRAM filled first; each probe ~199,000 cycles after VBlank's IRQ, then a fresh
 * Timer 0 reset, s = 64 + 4 i cycles after it, i = 0..539; D binary-searched for bit 26 between the shape's blanking D - 32
 * and + 1,024), the shapes VRAM copies (80h) from (512,256) to (192,128): 16x16, 64x16, 64x64, 256x8, 256x32, 128x16.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 540 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time70 --ntsc --frames=90000 --quiet=90000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time70 --emu --bios=<the model's>).
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    copyS("copy 16x16", 16, 16); copyS("copy 64x16", 64, 16); copyS("copy 64x64", 64, 64); copyS("copy 256x8", 256, 8);
    copyS("copy 256x32", 256, 32); copyS("copy 128x16", 128, 16);
}
`);
rep('const uint32_t lo = base[m][i] > 32 ? base[m][i] - 32 : 0, hi = base[m][i] + 480;', 'const uint32_t lo = base[m][i] > 32 ? base[m][i] - 32 : 0, hi = base[m][i] + 1024;');
s = s.split('gpu/gpu-shape-time69').join('gpu/gpu-shape-time70');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time70.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
