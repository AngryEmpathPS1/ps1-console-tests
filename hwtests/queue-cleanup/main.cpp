/*
 * misc/queue-cleanup - WHAT A FUZZ JOB CAN LEAVE THAT THE RUNNER'S START DOES NOT PUT BACK (2026-11-17, main: fuzz jobs left GP0(03h)
 * Timing Control set on 3365 / 3405 - GP1(00h) does not clear it - and the next GPU timing tests read ~1 cycle off, the GPU
 * session's gpu-tri-scan). Not a test: tools/consolequeue.js runs it on each console after a job that pokes hidden state (a fuzz
 * job), logged CLEANUP <port>. runner.exe stays as it is (its build is a start variable).
 * WHAT IT SETS, each a measured value: GP0(03h) = 0 (the slow-VRAM-boot finding: dma-len left it 2, and 0 cured it - memory "Slow
 * VRAM boot"); the SPU's main volume 3FFFh / 3FFFh (hwtests/spu-health "as found", the SCPH-7501 at Unirom). The rest of the fuzzer's
 * list (tools/fuzzall.js's op generators, its host's own restores - the console fuzzer, 2026-11-17) the runner already resets
 * before every test: the GPU (GP1(00h), so E1h-E6h), the MDEC, every voice, DPCR / DICR, timers 0 and 1, I_MASK.
 * WHAT IT ONLY READS, never measured at Unirom's menu: the reverb output volume (1F801D84h), the CD and external volumes (DB0h, DB4h)
 * and Timer 2's mode and target - printed as found, so these logs measure them (the fuzz host sets the volumes to 0; nothing puts
 * them back until their console values are known).
 * Prints the found values, what it set, and "Done.". No interrupts touched; the GPU wait is bounded.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define GP0      0x1F801810u
#define GPUSTAT  0x1F801814u
#define SPU(o)   (0x1F801C00u + (o))
#define T2_MODE  0x1F801124u
#define T2_TGT   0x1F801128u

int main() {
    printf("\nmisc/queue-cleanup\nWhat a fuzz job can leave that the runner's start does not put back. Not a test.\n\n");
    printf("as found: MAINVOL %04x %04x  REVVOL %04x %04x  CDVOL %04x %04x  EXTVOL %04x %04x  T2 mode %04x target %04x  GPUSTAT %08x\n",
           read16(SPU(0x180)), read16(SPU(0x182)), read16(SPU(0x184)), read16(SPU(0x186)), read16(SPU(0x1B0)), read16(SPU(0x1B2)),
           read16(SPU(0x1B4)), read16(SPU(0x1B6)), read16(T2_MODE) & 0x3FF, read16(T2_TGT), read32(GPUSTAT));
    int ready = 0;
    for (int i = 0; i < 1000000; i++) if (read32(GPUSTAT) & 0x04000000u) { ready = 1; break; }
    if (ready) write32(GP0, 0x03000000u);                       /* GP0(03h) = 0 */
    write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF);    /* the main volume as Unirom's shell leaves it */
    printf("set: GP0(03h)=0 %s, MAINVOL 3fff 3fff\n", ready ? "written" : "NOT written (the GPU never ready)");
    printf("after: MAINVOL %04x %04x\n", read16(SPU(0x180)), read16(SPU(0x182)));
    printf("misc/queue-cleanup: Done.\n");
    return 0;
}
