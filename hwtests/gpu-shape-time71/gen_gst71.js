'use strict';
// Builds hwtests/gpu-shape-time71/main.cpp from gpu-shape-time70's: one long copy (256x32) with its source and destination
// in different places of VRAM, at gpu-shape-time69's exact starts. node hwtests/gpu-shape-time71/gen_gst71.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time70', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time71 - DOES WHERE A COPY READS AND WRITES DECIDE WHAT THE SCAN-OUT COSTS IT? One copy, 256x32, with its
 * source and destination in six different places of VRAM, at gpu-shape-time69's exact starts, in 320x240 and 512x240
 * 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time70 on the SCPH-7501: a long copy (256x32 in 320x240) pays the scan-out in a clean sawtooth - its toll
 * falls one cycle a cycle of later start for one 128-pixel block's time (~258 ticks), then jumps back: it ENDS at the same
 * moment whatever its start, its blocks locked to the refills' grid (1,024 ticks, ~4 blocks). gpu-shape-time69's one-burst
 * kernels, applied burst by burst, do not lock (scratch/copysim.js). One mechanism that would: VRAM's banks - the copy's
 * reads and writes in different banks, a refill (the displayed area's rows) blocking only one stream - and gpu-shape-time68c
 * found the runs' spread depends on VRAM in some way. If the place matters, the toll changes with it; if not, banks are
 * not the lock.
 *
 * WHAT IT DOES. As gpu-shape-time70 (VRAM filled first; 540 starts 4 cycles apart from Timer 0's reset ~93 lines after
 * VBlank; D binary-searched within the blanking's D - 32 .. + 1,024), the copy (80h) 256x32 from / to: A (512,256) ->
 * (192,128) - gpu-shape-time70's; B (512,256) -> (704,288) - both off screen, right half; C (0,288) -> (192,128) - the
 * source left, below the display; D (512,0) -> (768,64) - both in the top right, off screen; E (192,128) -> (512,288) - the
 * source ON the displayed area; F (0,0) -> (0,288) - the source the displayed rows' first pixels.
 *
 * READ IT LIKE THIS. Per mode and placement the blanking's D, then the 540 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time71 --ntsc --frames=90000 --quiet=90000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time71 --emu --bios=<the model's>).
 */
`);
rep(/static void copyS\(const char\* nm, int w, int h\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void copyP(const char* nm, int sx, int sy, int dx, int dy, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4;
    s.w[0] = 0x80000000u; s.w[1] = xy(sx, sy); s.w[2] = xy(dx, dy); s.w[3] = xy(w, h);
}
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    copyP("copy A", 512, 256, 192, 128, 256, 32); copyP("copy B", 512, 256, 704, 288, 256, 32);
    copyP("copy C", 0, 288, 192, 128, 256, 32); copyP("copy D", 512, 0, 768, 64, 256, 32);
    copyP("copy E", 192, 128, 512, 288, 256, 32); copyP("copy F", 0, 0, 0, 288, 256, 32);
}
`);
// the rectangle helper is unused here
rep(/static void rect\(const char\* nm, int w, int h\) \{[\s\S]*?\n\}\n/.exec(s)[0], '');
s = s.split('gpu/gpu-shape-time70').join('gpu/gpu-shape-time71');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time71.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
