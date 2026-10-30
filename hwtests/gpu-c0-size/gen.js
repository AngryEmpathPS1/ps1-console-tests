'use strict';
// Writes main.cpp beside it: GPUSTAT bit 27 k NOPs after a GP0(C0h)'s size word, for copies of different sizes and
// places. node hwtests/gpu-c0-size/gen.js, then node hwtests/build.js gpu-c0-size.
const fs = require('fs'), path = require('path');
const KS = [...Array.from({ length: 40 }, (_, i) => i), ...Array.from({ length: 22 }, (_, i) => 44 + 4 * i)];   // 0..39, 44..128
const nops = (n) => Array.from({ length: n }, () => '        "nop\\n"\n').join('');
let probes = '';
for (const k of KS) probes += `static uint32_t p_${k}(uint32_t xy, uint32_t wh) {
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
${nops(k)}        "lw %0, 0x1814($t0)\\n"
        "nop\\n"
        ".set reorder\\n"
        : "=r"(st) : "r"(xy), "r"(wh) : "t0", "t1", "t2", "memory");
    return st;
}
`;
const SIZES = [[2, 1, 0], [4, 1, 0], [8, 1, 0], [16, 1, 0], [32, 1, 0], [64, 1, 0], [128, 1, 0], [1024, 1, 0], [2, 2, 0], [64, 2, 0],
  [1024, 2, 0], [1024, 512, 0], [2, 1, 8], [2, 1, 14], [64, 1, 8]];
const src = `/*
 * gpu/c0-size - when GPUSTAT bit 27 rises after a GP0(C0h), by the copy's size and place (2026-10-10)
 *
 * WHY. hwtests/gpu-c0-cold2 (the SCPH-9001, three runs identical): Datel's StoreImage check, cold, size store at word 0,
 * read bit 27 already set 14 times in 16 on a 2x1 copy and never on 64x2, 1024x2 or 1024x512 - the bit rises later for a
 * bigger copy, which is why GameShark 2.42 (a 1024x512 boot copy) works. src/psx.js's GPU_C0_SETUP (14 cycles after the
 * GPU takes the size word) was measured on 2x1 copies only (gpu-c0-gaps, gpu-c0-datel, gpu-c0-ready). This times it by
 * size: width 2 to 1024 at one row, two rows, 1024x512, and at x = 8 and 14.
 *
 * WHAT IT DOES. gpu-c0-gaps' case A (GP0(01h), C0h, position, size back to back from cached code), k NOPs, one GPUSTAT
 * read; k = 0..39 then 44..128 by 4; each probe run twice and the second read kept; the copy drained word by word.
 * A row: the size, the first k at which bit 27 read set, and the bits.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: \`node hwtests/run.js gpu-c0-size\`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;

${probes}
static uint32_t (*const P[${KS.length}])(uint32_t, uint32_t) = { ${KS.map((k) => 'p_' + k).join(', ')} };
static const int KS[${KS.length}] = { ${KS.join(', ')} };
static const uint16_t SZ[${SIZES.length}][3] = { ${SIZES.map((s) => `{ ${s.join(', ')} }`).join(', ')} };

static void idle() { for (int i = 0; i < 100000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } }
static void drain(uint32_t words) {   /* wait for bit 27 (it may not be up yet), then read the copy out */
    for (int i = 0; i < 20000 && !(*GPU1 & 0x08000000); i++) { }
    for (uint32_t i = 0; i < words; i++) (void)*GPU0;
    for (int i = 0; i < 8 && (*GPU1 & 0x08000000); i++) (void)*GPU0;
}

int main() {
    printf("\\ngpu/c0-size\\n");
    printf("GPUSTAT bit 27 k NOPs after a GP0(C0h)'s size word, by the copy's size and place (k = 0..39, then 44..128 by 4).\\n");
    printf("A row: the copy, the first k with bit 27 set, the bits. No assertions.\\n\\n");
    EnterCriticalSection();
    for (unsigned s = 0; s < ${SIZES.length}; s++) {
        const uint32_t w = SZ[s][0], h = SZ[s][1], x = SZ[s][2];
        char bits[${KS.length + 1}]; int first = -1;
        for (int i = 0; i < ${KS.length}; i++) {
            uint32_t st = 0;
            for (int rep = 0; rep < 2; rep++) {
                idle(); *GPU1 = 0x04000000; idle();
                st = P[i](x, (h << 16) | w);
                drain((w * h + 1) / 2);
            }
            bits[i] = (st >> 27) & 1 ? '1' : '0';
            if (first < 0 && bits[i] == '1') first = KS[i];
        }
        bits[${KS.length}] = 0;
        printf("%4ux%-3u x=%-2u first %3d  %s\\n", (unsigned)w, (unsigned)h, (unsigned)x, first, bits);
    }
    ExitCriticalSection();
    printf("\\nDone.\\n");
    for (;;) {}
}
`;
fs.writeFileSync(path.join(__dirname, 'main.cpp'), src);
fs.writeFileSync(path.join(__dirname, 'Makefile'), 'TARGET = gpu-c0-size.elf\n\ninclude ../../common-test.mk\n');
console.log('wrote main.cpp,', KS.length, 'probes,', SIZES.length, 'sizes');
