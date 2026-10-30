'use strict';
// Builds hwtests/gpu-shape-time67/main.cpp from gpu-shape-time48's: a new header, four display modes this core's positional
// scan-out never had maps in, the positional kinds as shapes. node hwtests/gpu-shape-time67/gen_gst67.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time67 - THE SCAN-OUT IN THE MODES NEVER MAPPED: 256x240, 368x240, 640x240 24-bit and PAL 320x240, for every
 * kind this core charges by the bursts' places (2026-10-25 night).
 *
 * WHY. src/psx.js GPU_SCAN_POS / GPU_SCAN_RECT: polygons, rectangles, sprites, fills (and, held, lines) pay the scan-out's
 * bursts where they fall - the line-start fetch and a refill every 256 bytes of the line, spaced by the mode's dot clock -
 * measured in Unirom's 512x240 (gpu-shape-time46), 320x240 and 640x480i (gst47). KNOWN-OPEN there: "other display modes than
 * the runner's 512x240 (the refills by the mode's dot clock and width here, measured in 512 only), the PAL line".
 * gpu-shape-time66's lines in 640x240 24-bit fit the refill spacing this core predicts (35 / 71 for one or two) - this
 * asks the same of every kind, in the two narrowest modes (256: no refill at all? 368: the odd dot clock), in 24-bit,
 * and in PAL's 50 Hz line.
 *
 * WHAT IT DOES. As gpu-shape-time48 (32 starts 67 cycles apart across one line from 200,000 cycles after VBlank's IRQ; D
 * binary-searched for bit 26; each shape's blanking D first), under GP1(08h) 00h (256x240), 40h (368x240), 13h (640x240
 * 24-bit) and 09h (320x240 PAL), range 260h..C60h, lines 16..256; Unirom's mode restored after. From (192,128): flat
 * rectangles (60h) 16x16 and 256x8, semi (62h) 16x16 and 256x32, 4-bit and 15-bit sprites (65h) 64x16, a fill (02h) 64x16,
 * flat and gouraud triangles 16x16, a 16x16 4-bit textured triangle, a VRAM copy (80h) 64x16 from (512,256).
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 32 starts' D, each minus the blanking's. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time67 --ntsc --frames=240000 --quiet=240000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time67 --emu --bios=<the model's>).
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    rect("rect 16x16", 0x60u, 16, 16); rect("rect 256x8", 0x60u, 256, 8);
    rect("semi 16x16", 0x62u, 16, 16); rect("semi 256x32", 0x62u, 256, 32);
    sprite("spr4 64x16", 0x65u, 0, 64, 16); sprite("spr15 64x16", 0x65u, 2, 64, 16); rect("fill 64x16", 0x02u, 64, 16);
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("flat 16", 0x20, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("gour 16", 0x30, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 15, 0, 0, 15 }; poly10("tex4 16", 0x24, V, U, 0); }
    copyS("copy 64x16", 64, 16);
}
`);
rep(`    static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };
    static const uint32_t MODE[2] = { 0, 0x01 };
    static uint32_t base[2][20], r26[2][20][32];
    for (int m = 0; m < 2; m++) {
        waitReady();
        if (m == 1) {   /* mode 0 is Unirom's own, left as it is */`,
    `    static const char* const MN[4] = { "256x240 15-bit", "368x240 15-bit", "640x240 24-bit", "320x240 PAL 15-bit" };
    static const uint32_t MODE[4] = { 0x00, 0x40, 0x13, 0x09 };
    static uint32_t base[4][20], r26[4][20][32];
    for (int m = 0; m < 4; m++) {
        waitReady();
        {`);
rep(`    for (int m = 0; m < 2; m++) {
        printf("%s:\\n", MN[m]);`, `    for (int m = 0; m < 4; m++) {
        printf("%s:\\n", MN[m]);`);
s = s.split('gpu/gpu-shape-time48').join('gpu/gpu-shape-time67');
s = s.split('under two display modes').join('under four display modes');
s = s.split("A lone line or copy's last words").join("A shape's last words");
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'Makefile'), 'utf8').split('gpu-shape-time48').join('gpu-shape-time67');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
