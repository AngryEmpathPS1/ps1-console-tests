'use strict';
// Builds hwtests/gpu-shape-time76/main.cpp from gpu-shape-time74's: polygons and a rectangle at the exact starts across the
// line-start fetch. node hwtests/gpu-shape-time76/gen_gst76.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time74', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time76 - HOW EACH KIND MEETS THE LINE-START FETCH: a flat, a gouraud and a semi triangle and a flat
 * rectangle, 16x16, at gpu-shape-time74's exact starts 2 cycles apart across a line's end and the fetch, in 512x240 and
 * 320x240 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time74 / 75 on the SCPH-7501: the fetch with the line hold is one busy stretch that costs a probe paying
 * it whole 94-99 cycles (this core 100-107) and ends 30-40 ticks later than this core's. The kinds this core models with a
 * run-on (GPU_SCAN_POS's polygons, GPU_SCAN_RECT's rectangles) were fitted against the old placement: refitting the
 * fetch's geometry needs each kind's own answer at the same exact starts - a flat polygon held from its first word, a
 * shaded one running on, a semi one reading the frame buffer, a write-only rectangle.
 *
 * WHAT IT DOES. As gpu-shape-time74 (VRAM filled first; each probe ~199,000 cycles after VBlank's IRQ, then a fresh Timer
 * 0 reset and an exact delay to s = 1,500 + 2 i cycles, i = 0..299, the shape's last words stored back to back - a gouraud
 * triangle's first two written just before; D binary-searched for bit 26 within the blanking's D - 32 .. + 480), under
 * Unirom's own 512x240 and 320x240 15-bit: triangles (0,0)-(16,0)-(0,16) from (192,128) flat (20h), gouraud (30h) and
 * semi (22h), and a flat rectangle (60h) 16x16.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 300 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time76 --ntsc --frames=40000 --quiet=40000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time76 --emu --bios=<the model's>).
 */
`);
rep('struct Shape { const char* name; int n; uint32_t w[4]; };', 'struct Shape { const char* name; int n; uint32_t w[8]; };');
rep(/static void line3\(const char\* nm, uint32_t c, int dx, int dy\) \{[\s\S]*?\n\}\nstatic void rect3/.exec(s)[0], 'static void rect3');
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void tri(const char* nm, uint32_t c) {
    Shape& s = S[NS++]; s.name = nm; s.n = 0;
    const bool g = (c & 0x10u) != 0;
    s.w[s.n++] = (c << 24) | 0x404040u; s.w[s.n++] = xy(192, 128);
    if (g) s.w[s.n++] = 0x204080u;
    s.w[s.n++] = xy(208, 128);
    if (g) s.w[s.n++] = 0x802040u;
    s.w[s.n++] = xy(192, 144);
}
static void defs() {
    tri("flat tri 16", 0x20u); tri("gour tri 16", 0x30u); tri("semi tri 16", 0x22u); rect3("rect 16x16", 0x60u, 16, 16);
}
`);
rep(/static uint32_t probe\(const Shape& sh, uint32_t s, uint32_t d\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    if (sh.n <= 4) return sh.n == 3 ? probe3(sh.w, s, n, k) : probe4(sh.w, s, n, k);
    for (int i = 0; i < sh.n - 4; i++) write32(GP0, sh.w[i]);   /* the words before the last four, just before */
    return probe4(sh.w + sh.n - 4, s, n, k);
}
`);
s = s.split('gpu/gpu-shape-time74').join('gpu/gpu-shape-time76');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time76.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
