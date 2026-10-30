'use strict';
// Builds hwtests/gpu-shape-time68c/main.cpp from gpu-shape-time68's: the same shapes and modes with ALL of VRAM filled to one
// colour first. node hwtests/gpu-shape-time68c/gen_gst68c.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time68', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time68c - gpu-shape-time68 with ALL OF VRAM FILLED TO ONE COLOUR FIRST (2026-10-26).
 *
 * WHY. gpu-shape-time68 on the SCPH-7501, six runs: copies and textured shapes differ from run to run at the same start (a
 * copy 17-39 cycles, a textured triangle 10-18; sprites 2-12) though every run is the runner's locked start, and the same
 * shapes in the blanking (gpu-shape-time58) read alike on two consoles. What carries over from one run to the next?
 * One suspect: what the runs before left on the screen - VRAM's contents. The other suspect: the GPU's clock phase against
 * the CPU's at each run's start. This clears VRAM before every run: if the runs then agree, the contents; if not, not.
 *
 * WHAT IT DOES. As gpu-shape-time68, after filling all of VRAM (1024 x 512) with 404040h in four 512 x 256 fills, then
 * the test's own fills.
 *
 * READ IT LIKE THIS. As gpu-shape-time68: the runs against each other. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time68c --ntsc --frames=30000 --quiet=30000\`.
 */
`);
rep(`    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));`,
    `    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02404040u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));`);
s = s.split('gpu/gpu-shape-time68').join('gpu/gpu-shape-time68c');
s = s.split('gpu-shape-time68cc').join('gpu-shape-time68c');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
const mk = fs.readFileSync(path.join(R, 'gpu-shape-time68', 'Makefile'), 'utf8').split('gpu-shape-time68').join('gpu-shape-time68c');
fs.writeFileSync(path.join(__dirname, 'Makefile'), mk);
console.log('written');
