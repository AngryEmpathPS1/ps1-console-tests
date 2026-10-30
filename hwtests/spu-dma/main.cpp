/*
 * spu/spu-dma - what a DMA to and from sound RAM really delivers (2026-10-06, after spu-reverb's two test bugs).
 *
 * WHY. spu-reverb on the three consoles found two behaviours this core does not have:
 *   - A DMA READ with 1F801014h at the BIOS's 200931E1h comes back unstable. psx-spx, "SPU RAM DMA-Read (unstable
 *     reading)": an FFFFh inserted at each 32-halfword block's start, the block's last halfword lost, and some
 *     halfwords "ORed by FFFFh (or ORed by other garbage values)"; 220931E1h ("bit24-27 MUST be nonzero") reads stable.
 *   - A DMA WRITE whose transfer mode is set to Stop as soon as DMA4 finishes lands only its first few halfwords. The
 *     SPU's 32-halfword FIFO still holds the rest and the stop drops it: a 64-byte upload landed 8 bytes. SPUSTAT bit 10
 *     (busy) waited on did not help; a fixed 100,000-cycle wait did.
 * This core lands every write at once and reads clean whatever 1F801014h holds, so neither can be modelled until it is
 * timed. This times both.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock. Sound RAM 2000h-20FFh is the work area. Every write that
 * prepares a trial is followed by a 100,000-cycle settle (spu-reverb's fix), and every check is read back STABLY
 * (220931E1h).
 *   TAIL  k = 0, 25, 50, 100, 200, 400, 800, 1600 and 3200 cycles. 64 halfwords of FFFFh written and settled; then 32
 *         halfwords of a pattern (halfword n = (n + 1) * 0101h, one 16-word block) by DMA, the transfer mode set to Stop
 *         k cycles after DMA4's busy bit clears; read back: how many of the 32 landed, and SPUSTAT bit 10 polled from the
 *         DMA's end until it has read 0 for 2,000 cycles (when it was set, when it cleared). Each k three times.
 *   READ  64 halfwords of 1000h + n written and settled; read back with 1F801014h = 220931E1h (the CONTROL) and then
 *         200931E1h (the BIOS's), at block size 16 (two blocks) and 17 (psx-spx's workaround: two blocks of 17 words),
 *         three times each: every halfword printed.
 *
 * READ IT LIKE THIS. TAIL: "landed n of 32" against k is the FIFO's drain to sound RAM (a halfword every so many cycles
 * after the DMA's end, where this core lands all 32 at once); "busy" is when bit 10 read 1 and when it cleared.
 * READ: the control must be 1000h..103Fh exactly. The BIOS-value rows are what psx-spx calls unstable: with block size
 * 16 it describes FFFFh, halfwords 00h..1Eh, FFFFh, 20h..3Eh; with 17, FFFFh, 00h..1Eh, 1Fh twice, and so on.
 *
 * THE RULE: run here first: `node hwtests/run.js spu-dma --frames=2000`. HERE AT v0.369: every k lands 32 of 32, bit
 * 10 never read set; the control and the BIOS-value rows alike 1000h..103Fh (this core reads clean at either value),
 * block size 17 reading four halfwords on past the pattern (0000h: the area beyond it). A console that differs has found
 * the timing to model.
 * THE CONSOLES (2026-10-06, all three; STATUS "The SCPH-9001's seven" on): TAIL k = 0 4-5 (each run's first trial 7),
 * 25 4-5, 50 5-6, 100 8-9, 200 12, 400 15-16, 800 29, 1600 32; bit 10 never set. READ exactly psx-spx's unstable rows.
 * HERE AT v0.372 (M372: the SPU's transfer accesses, psx-spx's logic-analyzer schedule): TAIL 6/4/4, 4/4/4, 5/5/5,
 * 8/8/8, 12, 15, 29, 32, 32 - the SCPH-7501's rows exactly but the cold first trial (6 for its 7); READ as the consoles.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SPUSTAT 0x1AE
#define AREA 0x2000u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(SPUSTAT)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void dmaStart(uint32_t spuAddr, void* ram, int words, int bs, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / bs) << 16) | (uint32_t)bs);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
}
static void dmaWait() { for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {} }
/* A write that lands whole (spu-reverb's fix): settle 100,000 cycles before the stop. */
static void writeSettled(uint32_t spuAddr, void* ram, int words) {
    dmaStart(spuAddr, ram, words, 16, true); dmaWait(); idle(100000); spuMode(0);
}
static void readStable(uint32_t spuAddr, void* ram, int words, int bs) {
    write32(0x1F801014, 0x220931E1);
    dmaStart(spuAddr, ram, words, bs, false); dmaWait(); spuMode(0);
}

static uint16_t fill[64] __attribute__((aligned(16)));
static uint16_t pat[32] __attribute__((aligned(16)));
static uint16_t back[80] __attribute__((aligned(16)));

struct Tail { int landed, run; int32_t busyOn, busyOff; };
static Tail tails[9][3];
static const uint32_t K[9] = { 0, 25, 50, 100, 200, 400, 800, 1600, 3200 };

static void tailTrial(uint32_t k, Tail& T) {
    for (int i = 0; i < 64; i++) fill[i] = 0xFFFF;
    writeSettled(AREA, fill, 32);
    for (int i = 0; i < 32; i++) pat[i] = (uint16_t)((i + 1) * 0x0101);
    dmaStart(AREA, pat, 16, 16, true);
    dmaWait();
    const uint32_t t0 = now();
    while (now() - t0 < k) {}
    spuMode(0);
    /* bit 10 from the DMA's end: when it read 1 first, when it read 0 again (2,000 cycles of 0 ends the watch) */
    T.busyOn = -1; T.busyOff = -1;
    uint32_t lastOne = 0; bool seen = false;
    for (;;) {
        const uint32_t t = now() - t0;
        const bool b = (read16(SPU(SPUSTAT)) & 0x0400) != 0;
        if (b) { if (!seen) { T.busyOn = (int32_t)t; seen = true; } lastOne = t; }
        else if (seen && T.busyOff < 0 && t - lastOne > 0) T.busyOff = (int32_t)t;
        if (b) T.busyOff = -1;
        if ((!seen && t > 2000) || (seen && !b && t - lastOne > 2000) || t > 200000) break;
    }
    idle(100000);
    readStable(AREA, back, 32, 16);
    T.landed = 0; T.run = 0;
    for (int i = 0; i < 32; i++) if (back[i] == pat[i]) T.landed++;
    while (T.run < 32 && back[T.run] == pat[T.run]) T.run++;
}

static uint16_t readRows[4][3][80];
static void readTrials() {
    static uint16_t src[64] __attribute__((aligned(16)));
    for (int i = 0; i < 64; i++) src[i] = (uint16_t)(0x1000 + i);
    writeSettled(AREA, src, 32);
    for (int v = 0; v < 4; v++)                         /* 0: stable bs16, 1: stable bs17, 2: BIOS bs16, 3: BIOS bs17 */
        for (int r = 0; r < 3; r++) {
            for (int i = 0; i < 80; i++) back[i] = 0xCCCC;
            write32(0x1F801014, v < 2 ? 0x220931E1 : 0x200931E1);
            const int bs = (v & 1) ? 17 : 16;
            dmaStart(AREA, back, bs * 2, bs, false); dmaWait(); spuMode(0);
            for (int i = 0; i < 80; i++) readRows[v][r][i] = back[i];
        }
    write32(0x1F801014, 0x220931E1);
}

int main() {
    printf("\nspu/spu-dma\n");
    printf("What a DMA to and from sound RAM delivers: an upload stopped k cycles after it ends, and reads at the BIOS's\n");
    printf("1F801014h. Cycles at the system clock. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);           /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    write16(SPU(0x1AC), 0x0004);                          /* transfer control: normal */
    cnt = 0xC000; write16(SPU(0x1AA), cnt);               /* SPU on, unmuted */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);       /* output volume 0 */
    for (int k = 0; k < 9; k++) for (int r = 0; r < 3; r++) tailTrial(K[k], tails[k][r]);
    readTrials();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("1F801014h at the start: %08x\n\n", (unsigned)spuDelay);
    printf("TAIL: 32 halfwords by DMA (one 16-word block), Stop k cycles after DMA4 ends; landed (first run unbroken),\n");
    printf("SPUSTAT bit 10 set / cleared, cycles from the DMA's end (-1 never):\n");
    for (int k = 0; k < 9; k++) {
        printf("  k=%4u:", (unsigned)K[k]);
        for (int r = 0; r < 3; r++) printf("  %2d of 32 (%2d)  busy %5d %5d", tails[k][r].landed, tails[k][r].run, (int)tails[k][r].busyOn, (int)tails[k][r].busyOff);
        printf("\n");
    }
    static const char* const NAME[4] = { "STABLE 220931E1h, block 16", "STABLE 220931E1h, block 17",
                                         "BIOS 200931E1h, block 16", "BIOS 200931E1h, block 17" };
    printf("\nREAD: 1000h + n written (64 halfwords), read back:\n");
    for (int v = 0; v < 4; v++) {
        const int n = ((v & 1) ? 17 : 16) * 4;              /* halfwords received: two blocks */
        for (int r = 0; r < 3; r++) {
            printf("  %s, run %d:\n", NAME[v], r + 1);
            for (int i = 0; i < n; i++) { printf("%s%04x", (i % 16) ? " " : "    ", readRows[v][r][i]); if (i % 16 == 15 || i == n - 1) printf("\n"); }
        }
    }
    printf("\nspu/spu-dma: Done.\n");
    for (;;) VSync(0);
}
