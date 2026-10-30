'use strict';
// Builds hwtests/gpu-shape-time74/main.cpp from gpu-shape-time69's: shapes that pay a burst whole, at exact starts 2 cycles
// apart across the line-start fetch and its tail. node hwtests/gpu-shape-time74/gen_gst74.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time69', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time74 - THE LINE-START FETCH, CLOSE UP: shapes that pay a scan-out burst whole, at exact starts 2 cycles
 * apart across the fetch and its tail into the next line, in 512x240 and 320x240 15-bit (2026-10-26).
 *
 * WHY. Three readings of the fetch disagree. The polygons, rectangles and lines here model it as ONE burst, 123 ticks at
 * tick 3392.5 (GPU_SCAN_POS, v0.377.195). gpu-shape-time69's copies fit it as THREE refill-like bursts, 47.5 ticks each,
 * 67.5 apart, from tick 3390 (512x240) / 18 earlier (320x240). gpu-shape-time51's close-up (2026-10-16, 10-cycle steps)
 * read two back to back, ending 50-80 cycles past the core's. And gpu-shape-time66's steep and semi lines starting at its
 * first start - in the fetch's tail - read 18-52 cycles cheap on this core, where a longer single fetch makes the rest of
 * its maps worse. A shape that pays every burst whole, started 2 cycles at a time across the region, draws the fetch's
 * shape as the wait it causes: where each part begins and ends, and the gaps between.
 *
 * WHAT IT DOES. As gpu-shape-time69 (VRAM filled first; each probe ~199,000 cycles after VBlank's IRQ, then a fresh Timer 0
 * reset - the display's start - and an exact delay to s; D binary-searched for bit 26 within the blanking's D - 32 ..
 * + 480), s = 1,500 + 2 i cycles after the reset, i = 0..299 (the line's end, the fetch for the next line and its tail),
 * shapes: a vertical flat line 1x64 (40h, (192,128)-(192,191): one pixel a row, pays every burst whole), a semi rectangle
 * 16x16 (62h), a fill 16x16 (02h) and a VRAM copy 16x1 (80h, from (512,256)).
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 300 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time74 --ntsc --frames=40000 --quiet=40000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time74 --emu --bios=<the model's>).
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void line3(const char* nm, uint32_t c, int dx, int dy) {
    Shape& s = S[NS++]; s.name = nm; s.n = 3;
    s.w[0] = (c << 24) | 0x408020u; s.w[1] = xy(192, 128); s.w[2] = xy(192 + dx, 128 + dy);
}
static void rect3(const char* nm, uint32_t c, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 3;
    s.w[0] = (c << 24) | 0x408020u; s.w[1] = xy(192, 128); s.w[2] = xy(w, h);
}
static void defs() {
    line3("line 1x64", 0x40u, 0, 63); rect3("semi 16x16", 0x62u, 16, 16); rect3("fill 16x16", 0x02u, 16, 16); copyS("copy 16x1", 16, 1);
}
`);
rep(/static void rect\(const char\* nm, int w, int h\) \{[\s\S]*?\n\}\n/.exec(s)[0], '');
rep('static const int NI = 540;', 'static const int NI = 300;');
s = s.split('search(S[i], 64 + 4 * j, lo, hi + 1)').join('search(S[i], 1500 + 2 * j, lo, hi + 1)');
rep('printf("bit 26\'s D in the blanking, then from s = 64 + 4 j cycles after Timer 0\'s reset ~93 lines after VBlank less the blanking\'s, j = 0..539\\n");',
    'printf("bit 26\'s D in the blanking, then from s = 1,500 + 2 j cycles after Timer 0\'s reset ~93 lines after VBlank less the blanking\'s, j = 0..299\\n");');
s = s.split('gpu/gpu-shape-time69').join('gpu/gpu-shape-time74');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time74.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
