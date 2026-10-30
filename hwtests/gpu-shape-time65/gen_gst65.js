'use strict';
// Builds hwtests/gpu-shape-time65/main.cpp from gpu-shape-time58's: a new header, new shapes (poly-lines across one line's
// 32 starts). node hwtests/gpu-shape-time65/gen_gst65.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time58', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time65 - THE LINE HOLD IN A POLY-LINE, mapped across one line (2026-10-25 night).
 *
 * WHY. gpu-shape-time60-64 on the SCPH-7501 time each shape at eight starts: every diagonal and steep poly-line spreads 22-25
 * cycles over them (the new GPU's line hold, as a lone line feels it, GPU_SCAN_HOLD), a single tilted segment spreads as a
 * lone tilted line (1 x 64,32: 219..236) - but tilted and level poly-lines of 2-4 segments spread 0 (2 x 64,32 422..422,
 * 3 x 64,32 623..623, level 2 x 128 / 255). v0.377.192 charges the hold to a first segment and to a later steep one. Where
 * in a tilted poly-line does the hold vanish - a later segment's set-up absorbing it, the vertex taken under it, or the
 * eight starts missing it?
 *
 * WHAT IT DOES. As gpu-shape-time58 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26) at 32 starts 68
 * cycles apart (a whole line) from 20,000 cycles after VBlank's IRQ, all in the blanking. Flat lines (40h) from (192,128):
 * 64 x 32, 128 across, 64 x 64; flat poly-lines (48h, zigzag down dy and back, ended by 55555555h) of 1, 2 and 3 segments 64
 * x 32, 2 segments 32 x 16, 2 and 3 segments 128 across (level), 2 segments 64 x 64 (diagonal); a gouraud poly-line (58h)
 * of 2 segments 64 across.
 *
 * READ IT LIKE THIS. Per shape its fastest D, then the 32 starts' D less it: a plateau is the hold the drawing feels where a
 * line start falls in it, its length the drawing's. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time65 --ntsc --frames=80000 --quiet=80000\`.
 */
`);
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h: c 48h flat, 58h
   gouraud (the colours alternate 204080h / 408020h) */
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
    lineS("line 64x32", 0x40u, 64, 32);
    lineS("line 128", 0x40u, 128, 0);
    lineS("line 64x64", 0x40u, 64, 64);
    polyLine("poly1 64x32", 0x48, 1, 64, 32);
    polyLine("poly2 64x32", 0x48, 2, 64, 32);
    polyLine("poly3 64x32", 0x48, 3, 64, 32);
    polyLine("poly2 32x16", 0x48, 2, 32, 16);
    polyLine("poly2 128", 0x48, 2, 128, 0);
    polyLine("poly3 128", 0x48, 3, 128, 0);
    polyLine("poly2 64x64", 0x48, 2, 64, 64);
    polyLine("gpoly2 64", 0x58, 2, 64, 0);
}
`);
s = s.split('gpu/gpu-shape-time58').join('gpu/gpu-shape-time65');
s = s.split("A lone polygon's last words stored back to back").join("A shape's last words stored back to back");
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time58', 'Makefile'), 'utf8').split('gpu-shape-time58').join('gpu-shape-time65');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written', s.split('\n').length, 'lines');
