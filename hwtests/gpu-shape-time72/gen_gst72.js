'use strict';
// Builds hwtests/gpu-shape-time72/main.cpp from gpu-shape-time69's: one-row copies of more widths, and two mask-checked ones,
// at the same exact starts, in 512x240 only. node hwtests/gpu-shape-time72/gen_gst72.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time69', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time72 - A COPY'S KERNEL BY ITS WIDTH, AND WITH THE MASK CHECKED: one-row copies of 32, 48, 96, 160, 192 and
 * 320 pixels and mask-checked ones of 64 and 128, at gpu-shape-time69's exact starts, in 512x240 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time69 on the SCPH-7501 measured what one scan-out burst costs a copy by where it lands - a set-up the
 * burst waits out, then per 128-pixel block (or narrower row) a staircase, a free window and a ramp - for 16, 64, 128 and
 * 256 wide (the same in 512x240 and 320x240). A model of the copy as a pipeline on the bus (scratch/copypipe.js) needs the
 * shape at more widths: where the staircase's steps and the free window sit as a row grows from 16 to 128, and what a
 * block of 64 left over (160, 192, 320 wide) does. And psx-spx gives the new GPU's mask-checked copy "25.50 clks per 16pix
 * chunk": if the engine works in 16-pixel chunks, a mask-checked copy's kernel should show them.
 *
 * WHAT IT DOES. As gpu-shape-time69 (VRAM filled first; each probe from a fresh Timer 0 reset ~93 lines after VBlank, s =
 * 64 + 4 i cycles after it, i = 0..539; D binary-searched for bit 26 within the blanking's D - 32 .. + 480), under Unirom's
 * own 512x240 15-bit, copies (80h) from (512,256) to (192,128): 32x1, 48x1, 96x1, 160x1, 192x1, 320x1, and 64x1 and 128x1
 * with GP0(E6h) bit 1 set (the mask checked before each pixel; VRAM's mask bits are 0, so every pixel is written), E6h
 * back to 0 after.
 *
 * READ IT LIKE THIS. Per shape the blanking's D, then the 540 starts' D, each minus the blanking's (999 = past the window).
 * No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time72 --ntsc --frames=50000 --quiet=50000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time72 --emu --bios=<the model's>).
 */
`);
rep('struct Shape { const char* name; int n; uint32_t w[4]; };', 'struct Shape { const char* name; int n; uint32_t w[4]; uint32_t e6; };');
rep(/static void copyS\(const char\* nm, int w, int h\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void copyS(const char* nm, int w, int h, uint32_t e6 = 0) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4; s.e6 = e6;
    s.w[0] = 0x80000000u; s.w[1] = xy(512, 256); s.w[2] = xy(192, 128); s.w[3] = xy(w, h);
}
`);
rep(/static void rect\(const char\* nm, int w, int h\) \{[\s\S]*?\n\}\n/.exec(s)[0], '');
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    copyS("copy 32x1", 32, 1); copyS("copy 48x1", 48, 1); copyS("copy 96x1", 96, 1); copyS("copy 160x1", 160, 1);
    copyS("copy 192x1", 192, 1); copyS("copy 320x1", 320, 1); copyS("mask 64x1", 64, 1, 2); copyS("mask 128x1", 128, 1, 2);
}
`);
rep('static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };', 'static const char* const MN[1] = { "512x240 15-bit" };');
rep('static const uint32_t MODE[2] = { 0, 0x01 };', 'static const uint32_t MODE[1] = { 0 };');
rep('static uint16_t r26[2][8][NI];', 'static uint16_t r26[1][8][NI];');
rep('static uint32_t base[2][8];', 'static uint32_t base[1][8];');
rep('            waitReady(); sync(); probe(S[i], 64, 6000); waitReady(); sync(); probe(S[i], 64, 6000); waitReady();   /* the code warm */',
    '            waitReady(); write32(GP0, 0xE6000000u | S[i].e6);   /* the mask check, this shape\'s */\n            waitReady(); sync(); probe(S[i], 64, 6000); waitReady(); sync(); probe(S[i], 64, 6000); waitReady();   /* the code warm */');
s = s.split('for (int m = 0; m < 2; m++)').join('for (int m = 0; m < 1; m++)');
rep('    waitReady();\n    write32(GP1, 0x08000000u | ((stat0', '    waitReady(); write32(GP0, 0xE6000000u);\n    waitReady();\n    write32(GP1, 0x08000000u | ((stat0');
s = s.split('gpu/gpu-shape-time69').join('gpu/gpu-shape-time72');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time72.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
