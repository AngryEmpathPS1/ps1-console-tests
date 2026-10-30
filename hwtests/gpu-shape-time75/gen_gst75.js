'use strict';
// Builds hwtests/gpu-shape-time75/main.cpp from gpu-shape-time74's: the line-start fetch close up in the four modes
// gpu-shape-time67 mapped. node hwtests/gpu-shape-time75/gen_gst75.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time74', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time75 - THE LINE-START FETCH, CLOSE UP, BY MODE: gpu-shape-time74's probes in 256x240, 368x240, 640x240
 * 24-bit and 320x240 PAL 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time74 on the SCPH-7501 drew the fetch (with the line hold) as one busy stretch that ends 30-40 ticks later
 * than this core's in 512x240 and 320x240, and gpu-shape-time69's copies put it ~18 ticks earlier in 320x240 than in
 * 512x240 - both places about 5 dots before the line's end at the mode's dot clock (3,412.5 - 5 x 5 = 3,387.5; - 5 x 8 =
 * 3,372.5). This core puts it at one tick for every mode. Where it sits at dot dividers 10, 7 and 4, and in PAL's line,
 * decides between the two.
 *
 * WHAT IT DOES. As gpu-shape-time74 (VRAM filled first; each probe ~199,000 cycles after VBlank's IRQ, then a fresh Timer
 * 0 reset and an exact delay to s = 1,500 + 2 i cycles, i = 0..299; D binary-searched for bit 26 within the blanking's D
 * - 32 .. + 480; a vertical flat line 1x64, a semi rectangle 16x16, a fill 16x16, a copy 16x1), under GP1(08h) 00h
 * (256x240), 40h (368x240), 13h (640x240 24-bit) and 09h (320x240 PAL), range 260h..C60h, lines 16..256.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 300 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time75 --ntsc --frames=60000 --quiet=60000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time75 --emu --bios=<the model's>).
 */
`);
rep('static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };', 'static const char* const MN[4] = { "256x240 15-bit", "368x240 15-bit", "640x240 24-bit", "320x240 PAL 15-bit" };');
rep('static const uint32_t MODE[2] = { 0, 0x01 };', 'static const uint32_t MODE[4] = { 0x00, 0x40, 0x13, 0x09 };');
rep('static uint16_t r26[2][8][NI];', 'static uint16_t r26[4][8][NI];');
rep('static uint32_t base[2][8];', 'static uint32_t base[4][8];');
rep('        if (m == 1) {   /* mode 0 is Unirom\'s own, left as it is */', '        {');
s = s.split('for (int m = 0; m < 2; m++)').join('for (int m = 0; m < 4; m++)');
s = s.split('gpu/gpu-shape-time74').join('gpu/gpu-shape-time75');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time75.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
