'use strict';
// Writes main.cpp (and the Makefile) beside it: two sweeps of K probes - Datel's StoreImage order (GP0(01h) and
// GP0(C0h) back to back) and GP0(01h) alone - each k NOPs and then one GPUSTAT read.
// node hwtests/gpu-c0-datel/gen.js, then node hwtests/build.js gpu-c0-datel.
const fs = require('fs'), path = require('path');
const K = 64;
const nopsOf = (k) => Array.from({ length: k }, () => '        "nop\\n"\n').join('');
let probes = '';
for (let k = 0; k < K; k++) {
  probes += `static uint32_t datel${k}(uint32_t xy, uint32_t wh) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
        "lui $t1, 0x0100\\n"
        "lui $t2, 0xc000\\n"
        "sw $t1, 0x1810($t0)\\n"
        "sw $t2, 0x1810($t0)\\n"
        "sw %1, 0x1810($t0)\\n"
        "sw %2, 0x1810($t0)\\n"
${nopsOf(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t clear${k}() {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
        "lui $t1, 0x0100\\n"
        "sw $t1, 0x1810($t0)\\n"
${nopsOf(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : : "t0", "t1", "memory");
    return st;
}
`;
}
const list = (name) => Array.from({ length: K }, (_, k) => `${name}${k}`).join(', ');
const src = `/*
 * gpu/gpu-c0-datel - GPUSTAT bit 27 after a GP0(C0h) sent straight behind a GP0(01h), as Datel's StoreImage sends
 * it; and how long a GP0(01h) alone keeps the GPU from taking the next command.
 *
 * WHY (console day, 2026-10-05). M333 raised bit 27 25 cycles after a C0h's dispatch, so that Datel's StoreImage -
 * GP0(01h), GP0(C0h), position, size, then \`while (GPUSTAT & 0x08000000);\` and only then GPUREAD - reads the bit
 * clear at its check, 15 cycles on: with the bit up at once it waited for ever, and a product sold for consoles
 * should not. hwtests/gpu-c0-ready then asked two consoles, and the SCPH-5501 and SCPH-7501 both read the bit set
 * at k = 0 - but gpu-c0-ready sends its GP0(01h) and WAITS FOR THE GPU IDLE before the C0h. Datel does not wait. If
 * a cache clear takes time on a console, Datel's C0h waits in the FIFO behind it and its check comes first: the
 * bit's own rise is immediate, and what this emulator lacks is the clear's cost (GP0(01h) is free here).
 *
 * WHAT IT DOES. DATEL: for k = 0..${K - 1}: the GPU idle, DMA off (GP1(04h) = 0); then in one stretch of assembly
 * GP0(01h), GP0(C0h), position (0,0), size (2x1: one word), exactly k NOPs, one GPUSTAT read; then, once bit 27
 * is up, GPUREAD until it drops (at most 8 reads; one word is expected) and GPUSTAT again. CLEAR: for k = 0..${K - 1}: the GPU idle; GP0(01h), k NOPs,
 * one GPUSTAT read - bits 26 (ready for a command) and 28 (ready for DMA blocks). Every probe runs three times in a
 * row and the last is kept (a warm instruction cache: a NOP a cycle).
 *
 * READ IT LIKE THIS. DATEL's first k with bit 27 set, against gpu-c0-ready's (0 on both consoles): later means the
 * clear held the C0h; Datel's own check is a RAM load and a NOP after its size word (~13 cycles). CLEAR's first k
 * with bit 26 back is the clear's cost.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-c0-datel\`. ON THIS CORE AT v0.361 (GPU_C0_READY_DELAY 25,
 * GP0(01h) free): DATEL first set after 21 NOPs (gpu-c0-ready's 23 less the 01h's store), one GPUREAD each; CLEAR
 * ready at k = 0. A console with an immediate bit 27 (gpu-c0-ready, both) and a DATEL k past ~13 says the clear
 * holds the C0h, and CLEAR says for how long.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;

${probes}
static uint32_t (*const DATEL[${K}])(uint32_t, uint32_t) = { ${list('datel')} };
static uint32_t (*const CLEAR[${K}])() = { ${list('clear')} };

static void idle() { for (int i = 0; i < 100000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }

int main() {
    printf("\\ngpu/gpu-c0-datel\\n");
    printf("GP0(01h) then GP0(C0h) back to back (Datel's StoreImage), and GP0(01h) alone. No assertions.\\n\\n");
    EnterCriticalSection();
    static uint32_t got[${K}], after[${K}], reads[${K}], clr[${K}];
    for (int k = 0; k < ${K}; k++) {
        for (int rep = 0; rep < 3; rep++) {
            idle();
            *GPU1 = 0x04000000;                 /* DMA off */
            const uint32_t st = DATEL[k](0x00000000, 0x00010002);
            /* the transfer read out whole before the next probe: wait for 27 (it may not be up yet - the question),
               then GPUREAD until it drops */
            for (int i = 0; i < 100000 && !(*GPU1 & 0x08000000); i++) { }
            int n = 0;
            while (n < 8 && (*GPU1 & 0x08000000)) { (void)*GPU0; n++; }
            got[k] = st; reads[k] = n; after[k] = *GPU1;
        }
    }
    for (int k = 0; k < ${K}; k++) {
        for (int rep = 0; rep < 3; rep++) {
            idle();
            clr[k] = CLEAR[k]();
        }
    }
    idle();
    ExitCriticalSection();
    int first = -1;
    printf("DATEL (GP0(01h), C0h, position, size, k NOPs, GPUSTAT):\\n");
    for (int k = 0; k < ${K}; k++) {
        const int b27 = (got[k] >> 27) & 1;
        if (b27 && first < 0) first = k;
        printf("  k=%2d  GPUSTAT %08x  bit 27 %d   GPUREAD reads to clear it %d, after %08x\\n", k, (int)got[k], b27, (int)reads[k], (int)after[k]);
    }
    if (first < 0) printf("  bit 27 never read set within ${K - 1} NOPs\\n");
    else printf("  bit 27 first reads set after %d NOP(s)\\n", first);
    int ready = -1;
    printf("CLEAR (GP0(01h), k NOPs, GPUSTAT):\\n");
    for (int k = 0; k < ${K}; k++) {
        const int b26 = (clr[k] >> 26) & 1, b28 = (clr[k] >> 28) & 1;
        if (b26 && ready < 0) ready = k;
        printf("  k=%2d  GPUSTAT %08x  bit 26 %d  bit 28 %d\\n", k, (int)clr[k], b26, b28);
    }
    if (ready < 0) printf("  bit 26 never read set within ${K - 1} NOPs\\n");
    else printf("  bit 26 first reads set after %d NOP(s)\\n", ready);
    printf("Done.\\n");
    for (;;) { }
}
`;
const dir = __dirname;
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(path.join(dir, 'main.cpp'), src);
fs.writeFileSync(path.join(dir, 'Makefile'), 'TARGET = gpu-c0-datel.elf\n\n\ninclude ../../common-test.mk\n');
console.log('written');
