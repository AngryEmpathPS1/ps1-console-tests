'use strict';
// Writes main.cpp (and the Makefile) beside it: K probes, each GP0(C0h) + its two words, k NOPs, one GPUSTAT read.
// node hwtests/gpu-c0-ready/gen.js, then node hwtests/build.js gpu-c0-ready.
const fs = require('fs'), path = require('path');
const K = 64;
let probes = '';
for (let k = 0; k < K; k++) {
  const nops = Array.from({ length: k }, () => '        "nop\\n"\n').join('');
  probes += `static uint32_t probe${k}(uint32_t xy, uint32_t wh) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
        "lui $t1, 0xc000\\n"
        "sw $t1, 0x1810($t0)\\n"
        "sw %1, 0x1810($t0)\\n"
        "sw %2, 0x1810($t0)\\n"
${nops}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh) : "t0", "t1", "memory");
    return st;
}
`;
}
const table = `static uint32_t (*const PROBES[${K}])(uint32_t, uint32_t) = { ${Array.from({ length: K }, (_, k) => `probe${k}`).join(', ')} };`;
const src = `/*
 * gpu/gpu-c0-ready - how soon after GP0(C0h)'s last word GPUSTAT bit 27 ("Ready to send VRAM to CPU") reads set.
 *
 * WHY (M333). Datel's own StoreImage - GameShark CDX 3.4 (USA) after its intro movie, the PAR3 cartridges
 * (GameShark 2.41 and "2.8", Action Replay 2.8 and 2.81, Pro Action Replay 3) at start-up, GameShark Lite with a
 * button pressed during its load - issues GP0(C0h), its position and size, and then: if GPUSTAT.27 is ALREADY set,
 * waits for it to CLEAR - and only then reads GPUREAD. Sony's library waits for it to SET. Until M333 27 was set
 * here from the command on, so all of them waited for ever. A product sold for consoles should not, so on a console
 * 27 must still read clear at that check: 15 cycles after the command's dispatch in this emulator's accounting for
 * the six products' own paths, at every DRAM refresh phase; GameShark Lite's at 17, or 24 when a refresh meets the
 * load before it. amidog's psxtest_gpu VRAM FLAG, graded against a console, allows the bit to rise anywhere from 16
 * cycles to several hundred. So M333 raises it 25 cycles after the dispatch, the smallest delay at which every one
 * of those checks reads clear - a fit to a window, and this test asks a console where in it the bit really rises.
 *
 * WHAT IT DOES. For k = 0..${K - 1}: wait for the GPU idle (GPUSTAT bits 26 and 28), DMA off (GP1(04h) = 0), GP0(01h);
 * then, in one stretch of assembly, GP0(C0h), its position (0,0) and size (2x1: one word), exactly k NOPs, and
 * one read of GPUSTAT; then GPUREAD read once (the transfer ends) and GPUSTAT again. Every probe runs three times
 * in a row and the last is kept (the code then runs from a warm instruction cache, a NOP a cycle: the ${K} probes
 * together are larger than the cache, and a first version that ran them all twice read every one cold). It prints
 * bit 27 for each k, and the first k at which it reads set. Datel's own check is a RAM load and a NOP after its
 * size word.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-c0-ready\`. Until M333 27 was set at every k, k = 0
 * included. ON THIS CORE AT v0.333 (GPU_C0_READY_DELAY: 25 cycles after the dispatch): clear for k = 0..22, set from
 * k = 23 - the read lands two cycles after the NOPs. A console's first k replaces that: each NOP here is a cycle, so
 * GPU_C0_READY_DELAY moves by the difference. k runs to ${K - 1} because amidog leaves the bit room to rise that late.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;

${probes}
${table}

static void idle() { for (int i = 0; i < 100000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }

int main() {
    printf("\\ngpu/gpu-c0-ready\\n");
    EnterCriticalSection();
    uint32_t got[${K}], after[${K}];
    for (int k = 0; k < ${K}; k++) {
        /* three times in a row, the last kept: all ${K} probes are ~10 KB against a 4 KB instruction cache, so a
           probe is only warm straight after itself */
        for (int rep = 0; rep < 3; rep++) {
            idle();
            *GPU1 = 0x04000000;                 /* DMA off */
            *GPU0 = 0x01000000;                 /* clear the cache, as the discs do */
            idle();
            uint32_t st = PROBES[k](0x00000000, 0x00010002);
            uint32_t data = *GPU0;              /* GPUREAD: the one word */
            (void)data;
            got[k] = st; after[k] = *GPU1;
        }
    }
    int first = -1;
    for (int k = 0; k < ${K}; k++) {
        const int b27 = (got[k] >> 27) & 1;
        if (b27 && first < 0) first = k;
        printf("k=%2d  GPUSTAT %08x  bit 27 %d   after the read %d\\n", k, (int)got[k], b27, (int)((after[k] >> 27) & 1));
    }
    if (first < 0) printf("bit 27 never read set within %d NOPs\\n", ${K - 1});
    else printf("bit 27 first reads set after %d NOP(s)\\n", first);
    printf("Done.\\n");
    /* Interrupts back on before parking (console day, 2026-10-05): Unirom's debug handler hears the cable through
     * them, and after this program the SCPH-5501 went deaf until a reset. */
    ExitCriticalSection();
    for (;;) { }
}
`;
const dir = __dirname;
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(path.join(dir, 'main.cpp'), src);
fs.writeFileSync(path.join(dir, 'Makefile'), 'TARGET = gpu-c0-ready.elf\n\n\ninclude ../../common-test.mk\n');
console.log('written');
