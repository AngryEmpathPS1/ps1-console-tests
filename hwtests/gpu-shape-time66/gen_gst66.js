'use strict';
// Builds hwtests/gpu-shape-time66/main.cpp from gpu-shape-time48's: a new header, a third display mode (640x240 24-bit),
// new shapes (lines of every kind and slope, a poly-line). node hwtests/gpu-shape-time66/gen_gst66.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time66 - LINES IN THE ACTIVE DISPLAY, every kind and slope, mapped across a line in three modes
 * (2026-10-25 night).
 *
 * WHY. Every kind but lines pays the scan-out's bursts by where it starts (GPU_SCAN_POS); lines still pay an average a
 * line (GPU_SCAN_K.line). The positional candidate (experiments/_mklinepos194.js, _mklineposB.js; STATUS "Lines by their
 * places in the scan-out, raced again") takes gpu-shape-time48's line maps 8.2 -> 3.5 cycles but gpu-lines2's - each line
 * at ONE start, 200,000 cycles after VBlank, where this core starts it inside the line-start fetch - 22.8 -> 41.7, semi and
 * steep lines 40-99 cheap; and 640x240 24-bit, where the refills are twice as many, was never mapped. gst48 drew six
 * lines in two 15-bit modes. This maps every kind (flat, semi, gouraud), every slope (level, tilted, diagonal, steep,
 * upright), short and long, and a poly-line, in all three modes - j = 0 is gpu-lines2's start.
 *
 * WHAT IT DOES. As gpu-shape-time48 (32 starts 67 cycles apart across one line from 200,000 cycles after VBlank's IRQ;
 * D binary-searched for bit 26; each shape's blanking D first), under Unirom's own 512x240, 320x240 15-bit and 640x240
 * 24-bit (GP1(08h) 01h / 13h, range 260h..C60h, lines 16..256; Unirom's mode restored after). From (192,128): flat lines
 * (40h) 255 and 64 across, 255 x 32, 64 x 64, 64 and 255 down, 16 x 128; semi lines (42h) 255 and 64 across, 64 x 64 and
 * 64 down; gouraud lines (50h) 255 across and 64 x 64; a flat poly-line (48h) of 2 segments 64 across.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 32 starts' D, each minus the blanking's. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time66 --ntsc --frames=200000 --quiet=200000\`.
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* a flat poly-line from (192,128) of n segments w across, ended by 55555555h */
static void polyS(const char* nm, int n, int w) {
    Shape& s = add(nm, 2);
    W(s, 0x48000000u | F);
    for (int i = 0; i <= n; i++) W(s, xy(192 + w * i, 128));
    W(s, 0x55555555u);
}
static void defs() {
    lineS("line 255", 0x40u, 255, 0); lineS("line 64", 0x40u, 64, 0); lineS("line 255x32", 0x40u, 255, 32);
    lineS("line 64x64", 0x40u, 64, 64); lineS("line 64 dn", 0x40u, 0, 64); lineS("line 255 dn", 0x40u, 0, 255);
    lineS("line 16x128", 0x40u, 16, 128);
    lineS("semi 255", 0x42u, 255, 0); lineS("semi 64", 0x42u, 64, 0); lineS("semi 64x64", 0x42u, 64, 64);
    lineS("semi 64 dn", 0x42u, 0, 64);
    lineS("gour 255", 0x50u, 255, 0); lineS("gour 64x64", 0x50u, 64, 64);
    polyS("poly 2x64", 2, 64);
}
`);
rep(`    static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };
    static const uint32_t MODE[2] = { 0, 0x01 };
    static uint32_t base[2][20], r26[2][20][32];
    for (int m = 0; m < 2; m++) {
        waitReady();
        if (m == 1) {   /* mode 0 is Unirom's own, left as it is */`,
    `    static const char* const MN[3] = { "512x240 15-bit", "320x240 15-bit", "640x240 24-bit" };
    static const uint32_t MODE[3] = { 0, 0x01, 0x13 };
    static uint32_t base[3][20], r26[3][20][32];
    for (int m = 0; m < 3; m++) {
        waitReady();
        if (m >= 1) {   /* mode 0 is Unirom's own, left as it is */`);
rep(`    for (int m = 0; m < 2; m++) {
        printf("%s:\\n", MN[m]);`, `    for (int m = 0; m < 3; m++) {
        printf("%s:\\n", MN[m]);`);
s = s.split('gpu/gpu-shape-time48').join('gpu/gpu-shape-time66');
s = s.split('under two display modes').join('under three display modes');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time48', 'Makefile'), 'utf8').split('gpu-shape-time48').join('gpu-shape-time66');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
