'use strict';
// Writes main.cpp (and the Makefile) beside it: gpu-cmd-ready's method (the command's words from the CPU, k NOPs, one
// GPUSTAT read) for flat triangles of five sizes, a flat quad, a gouraud and a textured triangle.
// node hwtests/gpu-tri-ready/gen.js, then node hwtests/build.js gpu-tri-ready.
const fs = require('fs'), path = require('path');
const KS = [...Array.from({ length: 48 }, (_, k) => k), ...Array.from({ length: 25 }, (_, i) => 48 + i * 8)];   // 0..47, then 48..240 by 8
const K = KS.length;
const nopsOf = (k) => Array.from({ length: k }, () => '        "nop\\n"\n').join('');
const CMDS = [   // name, words, what
  ['t1', 4, 'GP0(20h) flat triangle, legs of 1'],
  ['t2', 4, 'GP0(20h) flat triangle, legs of 2'],
  ['t4', 4, 'GP0(20h) flat triangle, legs of 4'],
  ['t8', 4, 'GP0(20h) flat triangle, legs of 8'],
  ['t16', 4, 'GP0(20h) flat triangle, legs of 16'],
  ['q8', 5, 'GP0(28h) flat quad 8x8'],
  ['g8', 6, 'GP0(30h) gouraud triangle, legs of 8'],
  ['x8', 7, 'GP0(24h) textured triangle, legs of 8 (4-bit)'],
];
let probes = '';
for (const [name, words] of CMDS) {
  for (let i = 0; i < K; i++) {
    const k = KS[i];
    let sw = '';
    for (let w = 0; w < words; w++) sw += `        "sw %${w + 1}, 0x1810($t0)\\n"\n`;
    probes += `static uint32_t ${name}_${i}(const uint32_t* c) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
${sw}${nopsOf(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : ${Array.from({ length: words }, (_, w) => `"r"(c[${w}])`).join(', ')} : "t0", "memory");
    return st;
}
`;
  }
}
const list = (name) => Array.from({ length: K }, (_, k) => `${name}_${k}`).join(', ');
const src = `/*
 * gpu/gpu-tri-ready - when a small polygon's words leave the FIFO (GPUSTAT.28: its drawing starts) and when it is done
 * (bit 26), to the cycle, by size (2026-10-10).
 *
 * WHY. gpu-cmd-ready's legs-of-8 flat triangle releases the FIFO at k = 31 and finishes at 88 on all four consoles: some 25
 * cycles of set-up after its last word, ~57 of drawing. v0.377.71 splits it 34 / 48 (set-up 0.626 of the old table,
 * rows 2.38 + max(5.75, 0.83 x span)); a set-up of 0.5 and a start-up of 13 ticks a triangle gives the consoles' split -
 * but one size cannot say whether the drawing's fixed part is a start-up or the rows', and Sony's SMESH sample (~5,000
 * tiny flat triangles a frame) runs at 60 frames a second under one and at 30 under the other. This times flat triangles
 * with legs of 1, 2, 4, 8 and 16, a flat 8x8 quad, and gouraud and 4-bit textured triangles with legs of 8, the same way.
 *
 * WHAT IT DOES. As gpu-cmd-ready: interrupts off, the GPU idle, then in one stretch of assembly the command's words to
 * GP0, k NOPs (k = 0..47, then 48..240 by 8), one GPUSTAT read; each probe three times and the last kept. Drawn at y >=
 * 256 (off the display), the texture (texpage 512,0) whatever VRAM holds there.
 *
 * READ IT LIKE THIS. Per command, bits 26 and 28 at every k as a row of 0/1, and the first k each is set. The read lands
 * about k + 2 cycles after the last word's store. No assertions.
 * ON THIS CORE AT v0.377.71 (bit 26 / bit 28 first set): legs 1 30 / 30, legs 2 42 / 26, legs 4 47 / 40, legs 8 88 / 32,
 * legs 16 136 / 37; the 8x8 quad 136 / 96; gouraud legs 8 232 / 184; textured legs 8 200 / 128.
 * THE RULE: run it here first: \`node hwtests/run.js gpu-tri-ready\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;

${probes}
${CMDS.map(([n]) => `static uint32_t (*const P_${n}[${K}])(const uint32_t*) = { ${list(n)} };`).join('\n')}

static const int KS[${K}] = { ${KS.join(', ')} };
static void idle() { for (int i = 0; i < 200000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }

int main() {
    initVideo(320, 240);
    printf("\\ngpu/gpu-tri-ready\\n");
    printf("Small polygons from the CPU, then k NOPs and one GPUSTAT read. No assertions.\\n\\n");
    EnterCriticalSection();
    *GPU1 = 0x04000000;                                  /* DMA off */
    *GPU0 = 0xE3000000; *GPU0 = 0xE4000000 | (511u << 10) | 1023u; *GPU0 = 0xE5000000;   /* drawing area: all of VRAM */
    *GPU0 = 0xE1000000 | (0u << 7) | 8u;                 /* texpage 512,0, 4-bit */
    static const uint32_t C[${CMDS.length}][7] = {
        { 0x20102030u, (320u << 16) | 0u, (320u << 16) | 1u, (321u << 16) | 0u, 0, 0, 0 },      /* legs 1 */
        { 0x20102030u, (320u << 16) | 0u, (320u << 16) | 2u, (322u << 16) | 0u, 0, 0, 0 },      /* legs 2 */
        { 0x20102030u, (320u << 16) | 0u, (320u << 16) | 4u, (324u << 16) | 0u, 0, 0, 0 },      /* legs 4 */
        { 0x20102030u, (320u << 16) | 0u, (320u << 16) | 8u, (328u << 16) | 0u, 0, 0, 0 },      /* legs 8 */
        { 0x20102030u, (320u << 16) | 0u, (320u << 16) | 16u, (336u << 16) | 0u, 0, 0, 0 },     /* legs 16 */
        { 0x28102030u, (320u << 16) | 0u, (320u << 16) | 8u, (328u << 16) | 0u, (328u << 16) | 8u, 0, 0 },   /* quad 8x8 */
        { 0x30102030u, (320u << 16) | 0u, 0x00304050u, (320u << 16) | 8u, 0x00506070u, (328u << 16) | 0u, 0 },   /* gouraud legs 8 */
        { 0x24808080u, (320u << 16) | 0u, (480u << 22) | 0u, (320u << 16) | 8u, (8u << 16) | (0u << 8) | 8u, (328u << 16) | 0u, 0x0800u },   /* textured legs 8: CLUT (0,480), texpage 512,0 4-bit, uv (0,0) (8,0) (0,8) */
    };
    static const char* N[${CMDS.length}] = { ${CMDS.map(([, , what]) => `"${what}"`).join(', ')} };
    uint32_t (*const* P[${CMDS.length}])(const uint32_t*) = { ${CMDS.map(([n]) => `P_${n}`).join(', ')} };
    static uint32_t v[${CMDS.length}][${K}];
    for (int c = 0; c < ${CMDS.length}; c++)
        for (int k = 0; k < ${K}; k++)
            for (int rep = 0; rep < 3; rep++) { idle(); v[c][k] = P[c][k](C[c]); }
    idle();
    ExitCriticalSection();
    for (int c = 0; c < ${CMDS.length}; c++) {
        printf("%s:\\n  26: ", N[c]);
        int f26 = -1, f28 = -1;
        for (int k = 0; k < ${K}; k++) { if (k == 48) printf(" "); printf("%d", (int)((v[c][k] >> 26) & 1)); if (f26 < 0 && ((v[c][k] >> 26) & 1)) f26 = KS[k]; }
        printf("\\n  28: ");
        for (int k = 0; k < ${K}; k++) { if (k == 48) printf(" "); printf("%d", (int)((v[c][k] >> 28) & 1)); if (f28 < 0 && ((v[c][k] >> 28) & 1)) f28 = KS[k]; }
        printf("\\n  bit 26 first set at k=%d, bit 28 at k=%d\\n", f26, f28);
    }
    printf("\\ngpu/gpu-tri-ready: Done.\\n");
    for (;;) { }
}
`;
const dir = __dirname;
fs.writeFileSync(path.join(dir, 'main.cpp'), src);
fs.writeFileSync(path.join(dir, 'Makefile'), 'TARGET = gpu-tri-ready.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
