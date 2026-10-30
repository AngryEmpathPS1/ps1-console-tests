'use strict';
// Writes main.cpp (and the Makefile) beside it: three sweeps of K probes of a GP0(A0h) upload sent back to back -
// SHORT (2x1, its one data word), LONG (256x1, its first of 128 words), HEADER (the three command words, no data) -
// each followed by k NOPs and one GPUSTAT read.
// node hwtests/gpu-a0-ready/gen.js, then node hwtests/build.js gpu-a0-ready.
const fs = require('fs'), path = require('path');
const K = 64;
const nopsOf = (k) => Array.from({ length: k }, () => '        "nop\\n"\n').join('');
let probes = '';
for (let k = 0; k < K; k++) {
  // a0 = position, a1 = size, a2 = data word (unused by HEADER)
  for (const [name, words] of [['up', 4], ['hdr', 3]]) {
    probes += `static uint32_t ${name}${k}(uint32_t xy, uint32_t wh, uint32_t data) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\\n"
        "lui $t0, 0x1f80\\n"
        "lui $t1, 0xa000\\n"
        "sw $t1, 0x1810($t0)\\n"
        "sw %1, 0x1810($t0)\\n"
        "sw %2, 0x1810($t0)\\n"
${words === 4 ? '        "sw %3, 0x1810($t0)\\n"\n' : ''}${nopsOf(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh), "r"(data) : "t0", "t1", "memory");
    return st;
}
`;
  }
}
const list = (name) => Array.from({ length: K }, (_, k) => `${name}${k}`).join(', ');
const src = `/*
 * gpu/gpu-a0-ready - how long a GP0(A0h) upload's words sit in the GPU's FIFO: GPUSTAT bits 26 and 28 k NOPs after
 * them (2026-10-05, for the SCPH-9001).
 *
 * WHY (console day). hwtests/gpu-a0-size sends GP0(A0h), its position, its size and one data word back to back and
 * reads GPUSTAT at once: both consoles read bits 26/27/28 as 000 there - not ready for a command, not ready for a
 * DMA block - and 001 or 101 a frame later, in every case, the 2x1 control (whose one word ends the command)
 * included. This emulator reads 101 / 001 at once: it takes an upload's words the moment they are written, where a
 * console's GPU is still draining them from its FIFO. How long that lasts, the single read cannot say.
 *
 * WHAT IT DOES. For k = 0..${K - 1}, the GPU idle first, in one stretch of assembly:
 *   SHORT  GP0(A0h), position (256, 500), size 2x1, its one data word, k NOPs, one GPUSTAT read;
 *   LONG   the same with size 256x1 (128 words), after its FIRST word - then the other 127, each after bit 28;
 *   HEADER the three command words alone (size 2x1, no data), k NOPs, one GPUSTAT read - then the word.
 * Every probe runs three times in a row and the last is kept (a warm instruction cache: a NOP a cycle).
 *
 * READ IT LIKE THIS. Each sweep's first k with bit 28 set is when the FIFO had drained (SHORT: and bit 26, the
 * command done). HEADER against SHORT splits the command's own setup from a data word's write.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-a0-ready\`. ON THIS CORE AT v0.366: every sweep ready at
 * k = 0 - SHORT bits 26 and 28 set, LONG 28 set and 26 clear (127 words to come), HEADER 28 set - the instant FIFO.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;

${probes}
static uint32_t (*const UP[${K}])(uint32_t, uint32_t, uint32_t) = { ${list('up')} };
static uint32_t (*const HDR[${K}])(uint32_t, uint32_t, uint32_t) = { ${list('hdr')} };

static void idle() { for (int i = 0; i < 100000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }
static void feed(int n) { for (int w = 0; w < n; w++) { for (int i = 0; i < 100000 && !(*GPU1 & 0x10000000); i++) { } *GPU0 = 0x7C1F7C1Fu; } }
static int firstSet(const uint32_t* v, int bit) { for (int k = 0; k < ${K}; k++) if ((v[k] >> bit) & 1) return k; return -1; }

int main() {
    initVideo(320, 240);
    printf("\\ngpu/gpu-a0-ready\\n");
    printf("A GP0(A0h) upload's words back to back, then k NOPs and GPUSTAT. No assertions.\\n\\n");
    EnterCriticalSection();
    *GPU1 = 0x04000000;                         /* DMA off: the CPU feeds the words */
    static uint32_t sh[${K}], lo[${K}], hd[${K}];
    for (int k = 0; k < ${K}; k++)
        for (int rep = 0; rep < 3; rep++) {
            idle(); sh[k] = UP[k]((500u << 16) | 256u, 0x00010002u, 0x03000000u);
            idle(); lo[k] = UP[k]((500u << 16) | 256u, 0x00010100u, 0x03000000u); feed(127);
            idle(); hd[k] = HDR[k]((500u << 16) | 256u, 0x00010002u, 0); feed(1);
        }
    idle();
    ExitCriticalSection();
    static const char* NAMES[3] = { "SHORT (2x1, its one word)", "LONG (256x1, its first word)", "HEADER (the command, no data)" };
    const uint32_t* SETS[3] = { sh, lo, hd };
    for (int s = 0; s < 3; s++) {
        printf("%s:\\n", NAMES[s]);
        for (int k = 0; k < ${K}; k++) {
            const uint32_t v = SETS[s][k];
            printf("  k=%2d  GPUSTAT %08x  26/27/28 %d%d%d\\n", k, (int)v, (int)((v >> 26) & 1), (int)((v >> 27) & 1), (int)((v >> 28) & 1));
        }
        printf("  bit 28 first set at k=%d, bit 26 at k=%d\\n", firstSet(SETS[s], 28), firstSet(SETS[s], 26));
    }
    printf("Done.\\n");
    for (;;) { }
}
`;
const dir = __dirname;
fs.mkdirSync(dir, { recursive: true });
fs.writeFileSync(path.join(dir, 'main.cpp'), src);
fs.writeFileSync(path.join(dir, 'Makefile'), 'TARGET = gpu-a0-ready.elf\n\n\ninclude ../../common-test.mk\n');
console.log('written');
