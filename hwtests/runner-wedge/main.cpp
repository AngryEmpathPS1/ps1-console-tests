/*
 * runner-wedge - leaves the machine as a fuzz program can (2026-11-19): the program the runner's restore (restoreKernel, "THE RESTORE'S GUARDS AND ITS STEP WORD"
 * in hwtests/runner/main.cpp) is rehearsed against. Not a test of the machine and nothing for a console day: run it under the runner, then run anything, and
 * the second program's start meets what this one left. The wedge is tooling's (experiments/cleanupsafe/wedge-main.cpp, the guarded cleanup's test), without the
 * cleanup: the two silences after a finished fuzz program (the PSone 08:03Z, the SCPH-9001 21:03Z, 2026-11-19) came with a runner restore in front of the queue's
 * cleanup, and what stopped them is not known.
 *   - DPCR's enables for DMA2 (GPU) and DMA4 (SPU) off, then both channels started (CHCR 01000201h: busy, from RAM): channels that cannot run;
 *   - SPUCNT's transfer mode set to DMA write (bit 5) with nothing sent;
 *   - GP0(A0h), a 16 x 16 rectangle from (0, 0), and no data: the GPU waiting for 128 words.
 * It prints the state it left, then "Done.". The runner's start puts DPCR, the CHCRs, the GPU and the SPU's registers back; what each of those steps found and how long
 * it took is RK (8012FC10h) after the next program's start: experiments/runnerguardtest.js reads it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define GP0     0x1F801810u
#define GPUSTAT 0x1F801814u
#define SPUCNT  0x1F801DAAu
#define SPUSTAT 0x1F801DAEu
#define CHCR(n) (0x1F801088u + 0x10u * (n))

int main() {
    printf("\nrunner-wedge\nLeaves DMA2 and DMA4 started with their DPCR enables off, the SPU in DMA-write mode and a GP0(A0h) waiting for its data. Not a test.\n\n");
    write32(0x1F8010F0u, read32(0x1F8010F0u) & ~0x00088800u);
    write32(CHCR(2), 0x01000201u); write32(CHCR(4), 0x01000201u);
    write16(SPUCNT, read16(SPUCNT) | 0x0020);
    write32(GP0, 0xA0000000u); write32(GP0, 0); write32(GP0, 0x00100010u);
    printf("left: GPUSTAT %08x  DMA2 %08x  DMA4 %08x  SPUCNT %04x  SPUSTAT %04x  DPCR %08x\n", read32(GPUSTAT), read32(CHCR(2)), read32(CHCR(4)), read16(SPUCNT), read16(SPUSTAT), read32(0x1F8010F0u));
    printf("\nrunner-wedge: Done.\n");
    return 0;
}
