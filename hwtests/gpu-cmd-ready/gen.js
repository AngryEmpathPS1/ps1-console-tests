'use strict';
// Writes main.cpp (and the Makefile) beside it: for six small drawing commands sent back to back from the CPU, K probes
// of the command's words, then k NOPs and one GPUSTAT read (gpu-a0-ready's method).
// node hwtests/gpu-cmd-ready/gen.js, then node hwtests/build.js gpu-cmd-ready.
const fs = require('fs'), path = require('path');
const KS = [...Array.from({ length: 48 }, (_, k) => k), ...Array.from({ length: 25 }, (_, i) => 48 + i * 8)];   // 0..47, then 48..240 by 8
const K = KS.length;
const nopsOf = (k) => Array.from({ length: k }, () => '        "nop\\n"\n').join('');
const CMDS = [   // name, words, what
  ['fill', 3, 'GP0(02h) fill 16x16'],
  ['rect', 3, 'GP0(60h) flat rectangle 16x16'],
  ['dot', 2, 'GP0(68h) flat 1x1 rectangle'],
  ['trect', 4, 'GP0(64h) textured rectangle 16x16 (4-bit)'],
  ['line', 3, 'GP0(40h) flat line, 16 across'],
  ['tri', 4, 'GP0(20h) flat triangle, legs of 8'],
];
let probes = '';
for (const [name, words] of CMDS) {
  for (let i = 0; i < K; i++) {
    const k = KS[i];
    let sw = '';
    for (let w = 0; w < words; w++) sw += `        "sw %${w + 1}, 0x1810($t0)\\n"\n`;
    probes += `static uint32_t ${name}${i}(const uint32_t* c) {
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
const list = (name) => Array.from({ length: K }, (_, k) => `${name}${k}`).join(', ');
const src = `/*
 * gpu/gpu-cmd-ready - when GPUSTAT's bits 26 and 28 come back after a small drawing command, to the cycle.
 *
 * WHY (2026-10-08, the consensus over gpu-cost-sweep's console runs). gpu-cost-sweep polls GPUSTAT after each primitive,
 * a poll every ~47 cycles. On the SCPH-5501, 7501 and 9001 every 3-word command - fill, flat rectangle, line - reads bit
 * 28 (ready for a DMA block) set at the SECOND poll (~140 cycles); this core has a fill's and a rectangle's at the FIRST
 * (91) and a line's at the second (138); a 4-word textured rectangle reads it at the first poll (107) on both, and a
 * 1x1 rectangle's bit 26 (the command done) comes at 140 there and 154 here. The decoder's intake (v0.377.21: a CPU's
 * GP0 words taken 5 + 2n cycles late) and each command's setup decide these, and the sweep's poll is too coarse to say.
 *
 * WHAT IT DOES. As gpu-a0-ready: interrupts off, the GPU idle, then in one stretch of assembly the command's words to
 * GP0, k NOPs (k = 0..47, then 48..240 by 8), one GPUSTAT read; each probe three times in a row and the last kept (a warm cache: a NOP
 * a cycle). Commands, drawn at y >= 256 (off the display):
${CMDS.map(([n, w, what]) => ` *   ${n.padEnd(6)}${what} (${w} words)`).join('\n')}
 *
 * READ IT LIKE THIS. Per command, bit 26 and bit 28 at every k as a row of 0/1 (a space before k = 48, from where the
 * step is 8), and the first k each is set. The read
 * lands about k + 2 cycles after the last word's store.
 *
 * ON THIS CORE AT v0.377.43: bit 28 at k = 6 (fill, rectangle), 4 (1x1), 7 (textured rectangle), 21 (line), 64
 * (triangle); bit 26 at 128 (fill), 224 (rectangle), 45 (1x1), 39 (line), 88 (triangle), past 240 (textured).
 *
 * THE CONSOLES (all four, every run the same): bit 28 at k = 13 / 13 / 13 / 7 (a blip; set from 12) / 21 / 31, bit 26 at
 * 120-136 / 184-224 / 20 / 152-232 / 44-56 / 88 (the 1001 the slower of each pair).
 * AT v0.377.71 (the rectangle's set-up GPU_RECT_SETUP 66.6, the flat polygon's GPU_FLAT_SETUP_K): bit 26 at 136 / 184 / 20 /
 * 176 / 39 / 88 - the 1x1 and 16x16 rectangles the consoles' - and bit 28 at 5 / 5 / 4 / 6 / 22 / 40: the rectangles' and
 * fills' still 7-9 cycles early on every console (KNOWN-OPEN, shared by both GPUs).
 * THE RULE: run it here first: \`node hwtests/run.js gpu-cmd-ready\`.
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
    printf("\\ngpu/gpu-cmd-ready\\n");
    printf("Six small drawing commands from the CPU, then k NOPs and one GPUSTAT read. No assertions.\\n\\n");
    EnterCriticalSection();
    *GPU1 = 0x04000000;                                  /* DMA off */
    *GPU0 = 0xE3000000; *GPU0 = 0xE4000000 | (511u << 10) | 1023u; *GPU0 = 0xE5000000;   /* drawing area: all of VRAM */
    *GPU0 = 0xE1000000 | (0u << 7) | 8u;                 /* texpage 512,0, 4-bit */
    static const uint32_t C[${CMDS.length}][4] = {
        { 0x02102030u, (256u << 16) | 0u,   (16u << 16) | 16u, 0 },          /* fill 16x16 at (0,256) */
        { 0x60102030u, (256u << 16) | 32u,  (16u << 16) | 16u, 0 },          /* rect 16x16 */
        { 0x68102030u, (256u << 16) | 64u,  0, 0 },                          /* 1x1 */
        { 0x64808080u, (256u << 16) | 96u,  (480u << 22) | (0u << 16) | 0u, (16u << 16) | 16u },   /* textured 16x16 */
        { 0x40102030u, (300u << 16) | 0u,   (300u << 16) | 16u, 0 },         /* line */
        { 0x20102030u, (320u << 16) | 0u,   (320u << 16) | 8u, (328u << 16) | 0u },   /* triangle */
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
    printf("\\ngpu/gpu-cmd-ready: Done.\\n");
    for (;;) { }
}
`;
const dir = __dirname;
fs.writeFileSync(path.join(dir, 'main.cpp'), src);
fs.writeFileSync(path.join(dir, 'Makefile'), 'TARGET = gpu-cmd-ready.elf\n\ninclude ../../common-test.mk\n');
console.log('written');
