'use strict';
// Builds hwtests/gpu-shape-time68/main.cpp from gpu-shape-time48's: a new header, copies and textured shapes across a line in
// 512x240 and 320x240. node hwtests/gpu-shape-time68/gen_gst68.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time68 - COPIES AND TEXTURED SHAPES IN THE ACTIVE DISPLAY, across a line, in 512x240 and 320x240
 * (2026-10-26).
 *
 * WHY. Two residuals of the scan-out maps (gpu-shape-time46-48, 51, 66, 67 on the SCPH-7501, against this core): (1) COPIES
 * pay GPU_COPY_TOLL's average, measured on gpu-copy3/4's W x 32 copies (320x240: 0.8% at 256 wide) - but gst48's 256 x 8
 * copy in 320x240 pays ~85 cycles over ~1,060 (8%) at its starts, and gst67's 64 x 16 copy 29-43 RMS off in every mode;
 * are the short copies and the long ones the same law? (2) The 15-BIT SPRITE (gst67) costs 20-35 cycles less in the
 * display than this core charges at most starts, in all four modes - the texel fetch's run-on (GPU_SCAN_POS texK / texA),
 * fitted on 4-bit shapes.
 *
 * WHAT IT DOES. As gpu-shape-time48 (32 starts 67 cycles apart across one line from 200,000 cycles after VBlank's IRQ; D
 * binary-searched for bit 26; each shape's blanking D first), under Unirom's own 512x240 and 320x240 15-bit (GP1(08h) 01h,
 * range 260h..C60h, lines 16..256). From (192,128): VRAM copies (80h) from (512,256) of 16x16, 64x16, 256x8, 256x32 and
 * 64x64; 4-bit sprites (65h) 16x16, 64x16 and 256x16, an 8-bit one 64x16, 15-bit ones 16x16, 64x16 and 256x16; 4-bit and
 * 15-bit textured triangles (24h) 64 x 64.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 32 starts' D, each minus the blanking's. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time68 --ntsc --frames=30000 --quiet=30000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time68 --emu --bios=<the model's>).
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    copyS("copy 16x16", 16, 16); copyS("copy 64x16", 64, 16); copyS("copy 256x8", 256, 8);
    copyS("copy 256x32", 256, 32); copyS("copy 64x64", 64, 64);
    sprite("spr4 16x16", 0x65u, 0, 16, 16); sprite("spr4 64x16", 0x65u, 0, 64, 16); sprite("spr4 256x16", 0x65u, 0, 256, 16);
    sprite("spr8 64x16", 0x65u, 1, 64, 16);
    sprite("spr15 16x16", 0x65u, 2, 16, 16); sprite("spr15 64x16", 0x65u, 2, 64, 16); sprite("spr15 256x16", 0x65u, 2, 256, 16);
    { static const int V[6] = { 0, 0, 64, 0, 0, 64 }, U[6] = { 0, 0, 63, 0, 0, 63 }; poly10("tex4 64", 0x24, V, U, 0); }
    { static const int V[6] = { 0, 0, 64, 0, 0, 64 }, U[6] = { 0, 0, 63, 0, 0, 63 }; poly10("tex15 64", 0x24, V, U, 2); }
}
`);
s = s.split('gpu/gpu-shape-time48').join('gpu/gpu-shape-time68');
s = s.split("A lone line or copy's last words").join("A shape's last words");
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'Makefile'), 'utf8').split('gpu-shape-time48').join('gpu-shape-time68');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
