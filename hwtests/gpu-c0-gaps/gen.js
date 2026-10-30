'use strict';
// Writes main.cpp (and the Makefile) beside it: GPUSTAT bit 27 k NOPs after a GP0(C0h)'s last word, for the clear
// (GP0(01h)) and the C0h written with different spacings. node hwtests/gpu-c0-gaps/gen.js, then
// node hwtests/build.js gpu-c0-gaps.
const fs = require('fs'), path = require('path');
const K = 40;
const nops = (n) => Array.from({ length: n }, () => '        "nop\\n"\n').join('');

/* The writes of one case, as assembly lines: $t0 = 1F800000h, %1 = position, %2 = size, %3 = &GP0 pointer in RAM. */
const CASES = [
  { id: 'A', what: 'GP0(01h), C0h, position, size back to back (Datel\'s order, gpu-c0-datel\'s DATEL)', body: () =>
    `        "sw $t1, 0x1810($t0)\\n"\n        "sw $t2, 0x1810($t0)\\n"\n        "sw %1, 0x1810($t0)\\n"\n        "sw %2, 0x1810($t0)\\n"\n` },
  { id: 'B', what: 'the products\' spacing: the GP0 pointer loaded from RAM before every store (GameShark 2.42, 80010CF8h)', body: () =>
    ['$t1', '$t2', '%1', '%2'].map((r) => `        "lw $t3, 0(%3)\\n"\n        "nop\\n"\n        "sw ${r}, 0($t3)\\n"\n`).join('') },
  ...[4, 8, 13].map((g) => ({ id: 'C' + g, what: `GP0(01h), C0h, position, size with ${g} NOPs between each`, body: () =>
    `        "sw $t1, 0x1810($t0)\\n"\n${nops(g)}        "sw $t2, 0x1810($t0)\\n"\n${nops(g)}        "sw %1, 0x1810($t0)\\n"\n${nops(g)}        "sw %2, 0x1810($t0)\\n"\n` })),
  { id: 'D', what: 'no clear: C0h, position, size back to back', body: () =>
    `        "sw $t2, 0x1810($t0)\\n"\n        "sw %1, 0x1810($t0)\\n"\n        "sw %2, 0x1810($t0)\\n"\n` },
];

let probes = '';
for (const c of CASES) {
  for (let k = 0; k < K; k++) {
    probes += `static uint32_t p${c.id}_${k}(uint32_t xy, uint32_t wh, volatile uint32_t **gp0) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
        "lui $t1, 0x0100\\n"
        "lui $t2, 0xc000\\n"
${c.body()}${nops(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh), "r"(gp0) : "t0", "t1", "t2", "t3", "memory");
    return st;
}
`;
  }
}
/* PROD: GameShark 2.42's StoreImage as compiled (80010CF8h-80010D4Ch): DMA off through GP1, then each GP0 store after
 * a load of the pointer, then its own check - a load of the GPUSTAT pointer, a NOP, the read. */
probes += `static uint32_t prod(uint32_t xy, uint32_t wh, volatile uint32_t **gp0, volatile uint32_t **gp1) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t1, 0x0100\\n"
        "lui $t2, 0xc000\\n"
        "lw $t3, 0(%3)\\n"
        "nop\\n"
        "sw $t1, 0($t3)\\n"
        "lw $t3, 0(%3)\\n"
        "nop\\n"
        "sw $t2, 0($t3)\\n"
        "lw $t3, 0(%3)\\n"
        "nop\\n"
        "sw %1, 0($t3)\\n"
        "lw $t3, 0(%3)\\n"
        "nop\\n"
        "sw %2, 0($t3)\\n"
        "lw $t3, 0(%4)\\n"
        "nop\\n"
        "lw %0, 0($t3)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh), "r"(gp0), "r"(gp1) : "t1", "t2", "t3", "memory");
    return st;
}
`;
const tables = CASES.map((c) => `static uint32_t (*const T${c.id}[${K}])(uint32_t, uint32_t, volatile uint32_t **) = { ${Array.from({ length: K }, (_, k) => `p${c.id}_${k}`).join(', ')} };`).join('\n');
const caseList = CASES.map((c) => `    { "${c.id}", "${c.what.replace(/"/g, '\\"')}", T${c.id} },`).join('\n');

const src = `/*
 * gpu/gpu-c0-gaps - GPUSTAT bit 27 after a GP0(C0h), with a GP0(01h) before it written at the spacings the products
 * use (2026-10-06, for the SCPH-9001).
 *
 * WHY. Console day's gpu-c0-datel: with GP0(01h), C0h, position and size BACK TO BACK, bit 27 first reads set k = 20
 * NOPs after the size word on the SCPH-7501 (both runs); gpu-c0-ready, the C0h sent after waiting for the GPU idle,
 * reads it set at k = 0 on both consoles. A cache clear that holds the next command 25 cycles fits both - and here
 * breaks every Datel product M333 fixed: their StoreImage is compiled C that loads the GP0 pointer from RAM before
 * each store (GameShark 2.42, 80010CF8h), so its four writes come ~14 cycles apart, the size word 41 cycles after the
 * clear, and the check 15 cycles after that finds the bit set and waits for ever. On a console they work. So does the
 * C0h's delay run from the CLEAR, or from the C0h itself, and why does gpu-c0-ready see none?
 *
 * WHAT IT DOES. For each case below and k = 0..${K - 1}: the GPU idle (bits 26 and 28), DMA off (GP1(04h) = 0); then in one
 * stretch of assembly the case's writes, exactly k NOPs, one GPUSTAT read; then GPUREAD until bit 27 drops (one word:
 * a 2x1 transfer) and GPUSTAT again. Every probe runs three times in a row and the last is kept (a warm instruction
 * cache: a NOP a cycle). Then PROD: GameShark 2.42's own sequence - DMA off, each store after its pointer's load, the
 * check after a load of the GPUSTAT pointer - 16 times: bit 27 AT THE PRODUCT'S CHECK.
 *   A   GP0(01h), C0h, position, size back to back - gpu-c0-datel's DATEL (the 7501: first set at k = 20)
 *   B   the products' spacing: the GP0 pointer loaded from RAM before every store
 *   C4, C8, C13   4, 8 and 13 NOPs between each of the four writes
 *   D   no clear: C0h, position, size back to back
 *
 * READ IT LIKE THIS. A row is the case's first k with bit 27 set, then the bit for every k. If the delay runs from the
 * clear, the first k falls as the gaps grow (A > C4 > C8 > C13) by about three gaps a step; if it runs from the C0h,
 * it stays put. D says whether a C0h with no clear before it starts at once. PROD must read 0 on a console (the
 * products run there).
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-c0-gaps --frames=3000\`. ON THIS CORE AT v0.367 (M333: bit 27
 * rises 25 cycles after the C0h starts, the clear free): A 23, B 20, C4 23, C8 23, C13 21, D 23 - the bit late after
 * every C0h - and PROD set 0 of 16. A SCRATCH CORE WITH THE CLEAR HOLDING THE NEXT COMMAND 25 CYCLES (2026-10-06,
 * fitted to gpu-c0-datel and gpu-c0-ready, both consoles exact): A 20, B 0, C4 2, C8 0, C13 0, D 0, PROD set 16 of 16 -
 * the product hangs, and so did all seven Datel products in tools/cdxtest.js and carttest.js. The three answers a
 * console can give: the cases all ~20 with D ~20 (M333's delay, but then why does gpu-c0-ready read 0?); falling with
 * the gap (a hold from the clear - and then PROD must read set, which the products running there contradict); or ~20
 * wherever the C0h follows a clear closely, D 0 and PROD 0 (a C0h soon after a clear starts late).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;
static volatile uint32_t *gp0ptr = (volatile uint32_t *)0x1F801810;   /* in RAM, as the products keep it */
static volatile uint32_t *gp1ptr = (volatile uint32_t *)0x1F801814;

${probes}
${tables}

struct Case { const char *id; const char *what; uint32_t (*const *t)(uint32_t, uint32_t, volatile uint32_t **); };
static const Case CASES[] = {
${caseList}
};

static void idle() { for (int i = 0; i < 100000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }
static void drain() {   /* the one-word transfer: wait for bit 27 (it may not be up yet), then read it out */
    for (int i = 0; i < 2000 && !(*GPU1 & 0x08000000); i++) { }
    for (int i = 0; i < 8 && (*GPU1 & 0x08000000); i++) (void)*GPU0;
}

int main() {
    printf("\\ngpu/gpu-c0-gaps\\n");
    printf("GPUSTAT bit 27 k NOPs after a GP0(C0h)'s last word, the clear (GP0(01h)) and the C0h at different spacings.\\n");
    printf("A row: the case, the first k with bit 27 set, then bit 27 for k = 0..${K - 1}. No assertions.\\n\\n");
    EnterCriticalSection();
    for (unsigned c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++) {
        char bits[${K + 1}]; int first = -1;
        for (int k = 0; k < ${K}; k++) {
            uint32_t st = 0;
            for (int rep = 0; rep < 3; rep++) {
                idle();
                *GPU1 = 0x04000000;                 /* DMA off */
                idle();
                st = CASES[c].t[k](0x00000000, 0x00010002, &gp0ptr);
                drain();
            }
            bits[k] = (st >> 27) & 1 ? '1' : '0';
            if (first < 0 && bits[k] == '1') first = k;
        }
        bits[${K}] = 0;
        printf("%-4s first %2d  %s  %s\\n", CASES[c].id, first, bits, CASES[c].what);
    }
    int set = 0;
    for (int i = 0; i < 16; i++) {
        idle(); *GPU1 = 0x04000000; idle();
        uint32_t st = prod(0x00000000, 0x00010002, &gp0ptr, &gp1ptr);
        drain();
        if ((st >> 27) & 1) set++;
    }
    printf("PROD  bit 27 at GameShark 2.42's own check: set %d of 16\\n", set);
    ExitCriticalSection();
    printf("\\nDone.\\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-c0-gaps.elf\n\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp (' + CASES.length + ' cases x ' + K + ' + PROD) and Makefile');
