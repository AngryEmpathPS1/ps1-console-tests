'use strict';
// Builds hwtests/gpu-shape-time73/main.cpp from gpu-shape-time72's: masked and unmasked copies of many sizes, the blanking's
// D only. node hwtests/gpu-shape-time73/gen_gst73.js
const fs = require('fs'), path = require('path');
const R = path.join(__dirname, '..');
let s = fs.readFileSync(path.join(R, 'gpu-shape-time72', 'main.cpp'), 'utf8');
const rep = (a, b) => { if (s.split(a).length !== 2) throw new Error('anchor ' + String(a).slice(0, 60)); s = s.replace(a, b); };
rep(s.slice(0, s.indexOf('#include <common.h>')), `/*
 * gpu/gpu-shape-time73 - WHAT CHECKING THE MASK COSTS A COPY: VRAM copies of eleven sizes with GP0(E6h) bit 1 clear and set,
 * in the vertical blanking (2026-10-26).
 *
 * WHY. gpu-shape-time72 on the SCPH-7501: a mask-checked copy takes longer in the blanking than a plain one - 64x1 110
 * cycles against 95, 128x1 194 against 165 - where this core charges them alike (its copy cost, GPU_COPY3, has no mask
 * term; only the pixel writes check it). psx-spx gives the new GPU "1.00 clks per pixel, with Mask Check, when width>=16"
 * plus "25.50 clks per 16pix chunk" against 1.25 a pixel without, and its own term below 16 wide. The law by width and
 * rows is what this measures.
 *
 * WHAT IT DOES. As gpu-shape-time72's blanking D (each probe 20,000 cycles after VBlank's IRQ, then a fresh Timer 0 reset
 * and 64 cycles; D binary-searched for bit 26 over 0..16,000), VRAM copies (80h) from (512,256) to (192,128) of 8x1, 16x1,
 * 32x1, 64x1, 128x1, 256x1, 512x1, 16x16, 64x16, 128x8 and 256x8, each with GP0(E6h) 0 and then 2 (the mask checked;
 * VRAM's mask bits are 0, so every pixel is written).
 *
 * READ IT LIKE THIS. Per shape: plain D, masked D, and the difference. No assertions.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-shape-time73 --ntsc --frames=3000 --quiet=3000\` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time73 --emu --bios=<the model's>).
 */
`);
rep('static Shape S[8];', 'static Shape S[32];');
rep(/static void defs\(\) \{[\s\S]*?\n\}\n/.exec(s)[0], `static void defs() {
    static const int W[11] = { 8, 16, 32, 64, 128, 256, 512, 16, 64, 128, 256 }, H[11] = { 1, 1, 1, 1, 1, 1, 1, 16, 16, 8, 8 };
    static const char* const NM[11] = { "copy 8x1", "copy 16x1", "copy 32x1", "copy 64x1", "copy 128x1", "copy 256x1", "copy 512x1",
        "copy 16x16", "copy 64x16", "copy 128x8", "copy 256x8" };
    for (int i = 0; i < 11; i++) { copyS(NM[i], W[i], H[i], 0); copyS(NM[i], W[i], H[i], 2); }
}
`);
rep('static uint16_t r26[1][8][NI];', 'static uint16_t r26[1][1][1];');
rep('static uint32_t base[1][8];', 'static uint32_t base[1][32];');
// blanking only: no active starts
rep(/            g_off = 199000u;\n            const uint32_t lo[^\n]*\n            for \(int j = 0; j < NI; j\+\+\)[^\n]*\n/.exec(s)[0], '');
// print pairs: plain, masked, difference
rep(/    printf\("bit 26's D in the blanking[\s\S]*?\n    printf\("\\ngpu\/gpu-shape-time72: Done.\\n"\);/.exec(s)[0],
`    printf("bit 26's D in the blanking (20,000 cycles after VBlank's IRQ, 64 after Timer 0's reset): plain, mask-checked, the difference\\n");
    for (int i = 0; i + 1 < NS; i += 2)
        printf("%-12s plain %5u  masked %5u  +%d\\n", S[i].name, (unsigned)base[0][i], (unsigned)base[0][i + 1], (int)base[0][i + 1] - (int)base[0][i]);
    printf("\\ngpu/gpu-shape-time72: Done.\\n");`);
s = s.split('gpu/gpu-shape-time72').join('gpu/gpu-shape-time73');
fs.writeFileSync(path.join(__dirname, 'main.cpp'), s);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-shape-time73.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
