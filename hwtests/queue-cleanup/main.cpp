/*
 * misc/queue-cleanup-safe - hwtests/queue-cleanup with every access guarded and a trail of breadcrumbs (2026-11-19, a
 * candidate: main swaps it in for queue-cleanup once tested). Two "silent after Done" hangs that day (the PSone 08:03Z after
 * shrink-12000-r2, the SCPH-9001 21:03Z after shrink-12702-r8) ended the cleanup's run with "0 of 4 bytes in 60000 ms" - the
 * runner took the program and never said DONE. Its log is no witness of where: the runner keeps a program's printf in RAM
 * and sends it only after DONE, so a hang anywhere - the runner's own start (restoreKernel) included - leaves it empty.
 *
 * BREADCRUMBS: one byte straight out of SIO1, ahead of the runner's DONE, before any register access ('a') and between each
 * group after it ('b'..'h', the list below). tools/hwrunner.js --crumbs skips them before the DONE and, if the DONE never
 * comes, names the ones that came: none = the hang was before this program's first line (the runner's start); 'a' alone =
 * the DMA registers' reads; and so on. Each send waits for SIO1's transmitter at most a bounded time.
 *   a  entered main, nothing touched      b  DMA CHCR 0-6 and DICR read    c  every running channel stopped
 *   d  the SPU and Timer 2 read             e  GPUSTAT read                  f  GP0(03h) step done
 *   g  the main volume step done           h  the read-back done (Done. follows)
 * WHAT IT DOES is queue-cleanup's (GP0(03h) = 0, the SPU's main volume 3FFFh / 3FFFh; the reverb, CD and external volumes and
 * Timer 2 only read; its header says why), plus: CHCR 0-6 and DICR read FIRST and printed (DMA registers, not the SPU's or
 * GPU's); any channel found running stopped (its start and trigger bits cleared) and waited for, before the SPU or GPU is
 * touched; GP0(03h) only into a GPU ready for a command word and a DMA block with DMA2 idle (else GP1(01h) - a GP1 write
 * never waits on the FIFO - and one more bounded wait); the main volume only into an SPU not busy, in no transfer mode, with
 * DMA4 idle. Each write skipped, and said so, past its bound. The "as found:" and "set:" lines keep queue-cleanup's form.
 * experiments/cleanupsafetest.js tests it, a wedged start included.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define GP0       0x1F801810u
#define GP1       0x1F801814u
#define GPUSTAT   0x1F801814u
#define SPU(o)    (0x1F801C00u + (o))
#define SPUCNT    SPU(0x1AA)
#define SPUSTAT   SPU(0x1AE)
#define T2_MODE   0x1F801124u
#define T2_TGT    0x1F801128u
#define CHCR(n)   (0x1F801088u + 0x10u * (n))
#define DICR      0x1F8010F4u
#define SIO1_DATA 0x1F801050u
#define SIO1_STAT 0x1F801054u
#define BOUND     1000000

static void crumb(char c) {
    for (int i = 0; i < 200000; i++) if (read16(SIO1_STAT) & 1) { write8(SIO1_DATA, (uint8_t)c); return; }
}
static int waitClear32(uint32_t a, uint32_t mask) { for (int i = 0; i < BOUND; i++) if (!(read32(a) & mask)) return 1; return 0; }
static int gpuReady() {
    for (int i = 0; i < BOUND; i++) if ((read32(GPUSTAT) & 0x14000000u) == 0x14000000u && !(read32(CHCR(2)) & 0x01000000u)) return 1;
    return 0;
}

int main() {
    crumb('a');
    uint32_t ch[7];
    for (int n = 0; n < 7; n++) ch[n] = read32(CHCR(n));
    const uint32_t dicr = read32(DICR);
    crumb('b');
    int ran = 0, stuck = 0;
    for (int n = 0; n < 7; n++) if (ch[n] & 0x11000000u) {
        ran |= 1 << n;
        write32(CHCR(n), ch[n] & ~0x11000000u);
        if (!waitClear32(CHCR(n), 0x01000000u)) stuck |= 1 << n;
    }
    crumb('c');
    const uint16_t mv0 = read16(SPU(0x180)), mv1 = read16(SPU(0x182)), rv0 = read16(SPU(0x184)), rv1 = read16(SPU(0x186));
    const uint16_t cd0 = read16(SPU(0x1B0)), cd1 = read16(SPU(0x1B2)), ex0 = read16(SPU(0x1B4)), ex1 = read16(SPU(0x1B6));
    const uint16_t cnt = read16(SPUCNT), st = read16(SPUSTAT), t2m = read16(T2_MODE) & 0x3FF, t2t = read16(T2_TGT);
    crumb('d');
    const uint32_t gs = read32(GPUSTAT);
    crumb('e');
    printf("\nmisc/queue-cleanup\nWhat a fuzz job can leave that the runner's start does not put back. Not a test. (guarded)\n\n");
    printf("at entry: CHCR %08x %08x %08x %08x %08x %08x %08x  DICR %08x  SPUCNT %04x  SPUSTAT %04x\n",
           ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], dicr, cnt, st);
    printf("step 1: DMA channels running %02x, stopped %02x%s\n", ran, ran & ~stuck, stuck ? " - SOME STILL BUSY" : "");
    printf("as found: MAINVOL %04x %04x  REVVOL %04x %04x  CDVOL %04x %04x  EXTVOL %04x %04x  T2 mode %04x target %04x  GPUSTAT %08x\n",
           mv0, mv1, rv0, rv1, cd0, cd1, ex0, ex1, t2m, t2t, gs);
    /* 2. GP0(03h) = 0, only into a GPU that can take it */
    int ready = gpuReady(), reset = 0;
    if (!ready) { write32(GP1, 0x01000000u); reset = 1; ready = gpuReady(); }
    if (ready) write32(GP0, 0x03000000u);
    crumb('f');
    printf("step 2: GPU %s%s\n", ready ? "ready" : "NOT ready", reset ? " after GP1(01h)" : "");
    /* 3. the main volume, only into an SPU not transferring */
    int spuOk = 0;
    for (int i = 0; i < BOUND; i++) if (!(read16(SPUSTAT) & 0x0400) && !(read16(SPUCNT) & 0x0030) && !(read32(CHCR(4)) & 0x01000000u)) { spuOk = 1; break; }
    if (spuOk) { write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF); }
    crumb('g');
    printf("step 3: SPU %s\n", spuOk ? "idle" : "NOT idle (busy, a transfer mode set, or DMA4 running)");
    printf("set: GP0(03h)=0 %s, MAINVOL %s\n", ready ? "written" : "NOT written (the GPU never ready)", spuOk ? "3fff 3fff" : "NOT written (the SPU never idle)");
    printf("after: MAINVOL %04x %04x\n", read16(SPU(0x180)), read16(SPU(0x182)));
    crumb('h');
    printf("misc/queue-cleanup: Done.\n");
    return 0;
}
