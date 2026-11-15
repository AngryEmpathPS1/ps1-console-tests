/*
 * gpu/gpu-dma-map - where in the video frame an upload by DMA2 in blocks of 8, 16, 32 and 64 words loses halfwords: gpu-version's
 * DMA row at about 4,000 fixed places of the frame, in four display set-ups, so the program that launches the test no longer
 * decides the answer (2026-11-15).
 *
 * WHY. gpu-version's DMA row (64 x 4 pixels = 128 words to VRAM 896,500 by DMA2 SyncMode 1, after GP1(04h) = 2, in blocks of 8, 16,
 * 32 and 64 words, read back) read 0 0 0 0 in 101 runs on nine models of this project, and lost halfwords at 64-word blocks now
 * and then on another farm's six consoles with the same exe: an SCPH-1000 in 3 of 6 runs (26, 6 and 256 halfwords), two SCPH-5501s
 * once in 9 each (28, 92). A 64-word block is more than GP0's 16-word FIFO, and nothing waits between its words. hwtests/dma-loss
 * here (an SCPH-5501, 2026-11-02) lost 16 or 32 words of 1,024-word uploads in blocks of 32 and 64 now and then, and none at 8 or 16;
 * gpu-a0mode lost words in bursts only while the display was being scanned out, none in vblank. gpu-version's two 64-word blocks
 * last about 150 cycles at ONE place in the frame. Under this project's runner every test starts a fixed time after a VBlank edge,
 * with the display as Unirom left it, so that place is the same on every run; under a monitor that prints live over SIO1 it moves
 * from run to run, and the display is whatever that monitor left. So the question is where, and in which display: a map.
 *
 * WHAT IT DOES. Interrupts off. Timer 1 counts lines (hblank clock, sync mode 1: reset to 0 at VBlank), Timer 2 the system clock.
 * One frame (one field when interlaced) is one pass. In a pass, at lines 2, 6, 10 ... of Timer 1 (every fourth, to 5 before the
 * frame's end) and at one place along the line - 34 k cycles after the CPU sees the line's Timer 1 tick, k = 0..63, so 0-2,142
 * of a 2,152-cycle line - the upload starts: one store to CHCR2 (the rectangle filled with 0000h by GP0(02h) and GP1(01h) before
 * it, then the A0h header, GP1(04h) = 2, MADR2 and BCR2 written before the wait). After it: CHCR2 polled until done (the transfer's
 * cycles), 1,000 cycles, zeros to GP0 until GPUSTAT bit 26 sets (the words the GPU still wanted: lost words a FIFO wrap left it
 * waiting for), GP1(01h), GP1(04h) = 0, and the rectangle read back through GP0(C0h) and compared as it is read. A point whose line
 * has gone by when its turn comes (the point before it took longer: words to pad) waits for that line in the next frame. No word of the
 * upload holds 0000h (gpu-version's pattern), so a halfword never written reads 0 and one written with another word's value does
 * not. 64 passes (k) for each block size, in four display set-ups, in this order:
 *   0 as found   the display the launcher left, untouched (this project's runner: Unirom's 512 x 240, display on)
 *   1 320x240    GP1(05h) 0, GP1(06h) 200h-C00h, GP1(07h) 10h-100h (GP1(00h)'s), 15-bit, the console's video standard, on
 *   2 640x480i   the same, 640 wide, interlaced
 *   3 off        set-up 1 with the display off (GP1(03h) = 1)
 * Each set-up waits 3 frames, counts the lines of 2 frames (the fewer is used), and runs one pass that is thrown away (a warm-up:
 * a warm cache and the same vsync) before its 256 passes that count. Nothing is printed or read between those passes; every
 * result stays in RAM and is printed at the end. About 1,050 frames: ~18 s NTSC, ~21 s PAL. The rectangle is saved first and put
 * back last; GP0(03h) and GP0(E6h) are set to 0 first. v2 (2026-11-15 evening, after nugget's tests/bus/load-timings): the five delay
 * registers at 1F801010h are printed as found, CDROM_DELAY and COM_DELAY pinned to a disc-less boot's 00020843h / 00031125h (a disc
 * boot leaves 0000132Ch in COM_DELAY) and all five put back at the end; and the program ends at `break 4, 0` after its Done line,
 * where psxmon sees a run end (our runner takes the CPU back at the Done line, before it). The v1 logs (dmamap-r*, -free-*) have neither. At the end the display mode and enable are put back as found; GP1(05h),
 * (06h) and (07h) cannot be read back, and are left at set-up 3's values.
 *
 * READ IT LIKE THIS. Per set-up and block size, `MAP s bs: lines x 64 places, P points (F waited a frame): N lost data` with three
 * histograms (value x points): halfwords wrong, halfwords read 0 (never written), and words the GPU still wanted; then the
 * transfer's cycles (CHCR2's store to bit 24 read clear) fewest / most. If any point lost data, the map: a row per line (Timer 1's
 * count, from the VBlank reset), a character per place along it (k 0..63, 34 cycles apart): '.' nothing wrong, else the halfwords
 * wrong in 16s, G 1-16, H 17-32, I 33-48 ... V 241-256 (letters past F, so no row reads as hex). The last lines,
 * `SUMMARY`, are gpu-version's row per set-up: points that lost data at blocks of 8 16 32 64. No assertions.
 *
 * ON THE CONSOLES (2026-11-15, eight runs on each of six): blocks of 8 and 16 never lose; blocks of 32 lose at 3-4% of the places and 64
 * at 6-9% on the new GPU (SCPH-101, 5501, 7001, 7501, 7502), on display lines only, at the line's start and, for 64, at places along it
 * that move with the width; the old GPU (SCPH-1001) at 5% and 7%, on every line, at the line's start only. The display off loses as much.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-map --frames=3000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u
#define RAM_SIZE 0x1F801060u
#define DELAY_REGS 0x1F801010u       /* BIOS, SPU, CD-ROM, Expansion 2, COM_DELAY (nugget load-timings' five) */

#define NS 4                    /* display set-ups */
#define NB 4                    /* block sizes */
#define NK 64                   /* places along a line */
#define NL 80                   /* lines a pass: every fourth from 2, to 314 (PAL) */
#define STEP 34                 /* cycles between places */
#define RX 896u
#define RY 500u

static uint32_t gstat() { return read32(GP1); }
static void gp0(uint32_t w) { write32(GP0, w); }
static void gp1(uint32_t w) { write32(GP1, w); }
static void waitIdle() { for (int i = 0; i < 400000 && (gstat() & 0x14000000u) != 0x14000000u; i++) {} }   /* 26 and 28 */
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

static void vramRead(int x, int y, int w, int h, uint32_t* out) {
    waitIdle();
    gp0(0xC0000000u); gp0(((uint32_t)y << 16) | (uint32_t)x); gp0(((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(gstat() & (1u << 27)); i++) {}
    for (int i = 0; i < (w * h + 1) / 2; i++) out[i] = read32(GP0);
}
static void vramWrite(int x, int y, int w, int h, const uint32_t* src) {   /* 16-word blocks: what both GPUs take */
    waitIdle();
    gp0(0xA0000000u); gp0(((uint32_t)y << 16) | (uint32_t)x); gp0(((uint32_t)h << 16) | (uint32_t)w);
    gp1(0x04000002u);
    write32(D2_MADR, (uint32_t)src & 0x1FFFFF);
    write32(D2_BCR, ((uint32_t)((w * h + 1) / 2 / 16) << 16) | 16u);
    write32(D2_CHCR, 0x01000201u);
    for (int i = 0; i < 400000 && (read32(D2_CHCR) & 0x01000000u); i++) {}
    gp1(0x04000000u);
    waitIdle();
}

static const int BS[NB] = { 8, 16, 32, 64 };
static uint32_t pat[NB][128];                                /* gpu-version's words: 1000h + 100h b + 2i, + 1 */
static uint32_t saved[128];
static uint16_t rWrong[NS][NB][NK][NL], rZero[NS][NB][NK][NL], rTime[NS][NB][NK][NL];
static uint8_t rLeft[NS][NB][NK][NL];
static uint16_t lines[NS], nPts[NS], frames2[NS][2], late[NS][NB];
static uint32_t entryDelay[5];
static uint32_t noSync, entryStat, gpuType, area3, area3was, ramSize, dpcrWas;

/* a VBlank: Timer 1 (reset to 0 at VBlank) counts down. Timer 2's wraps bound the wait (3,000,000 cycles, ~5 frames). */
static void frameStart() {
    uint16_t prev = read16(T1_COUNT), s = read16(T2_COUNT);
    uint32_t el = 0;
    for (;;) {
        const uint16_t t = read16(T1_COUNT);
        if (t < prev) return;
        prev = t;
        const uint16_t n = read16(T2_COUNT); el += (uint16_t)(n - s); s = n;
        if (el > 3000000u) { noSync++; return; }
    }
}
/* the highest count Timer 1 reaches before the next VBlank, + 1 */
static uint16_t frameLines() {
    frameStart();
    uint16_t prev = read16(T1_COUNT), top = prev, s = read16(T2_COUNT);
    uint32_t el = 0;
    for (;;) {
        const uint16_t t = read16(T1_COUNT);
        if (t < prev) return (uint16_t)(top + 1);
        prev = t; if (t > top) top = t;
        const uint16_t n = read16(T2_COUNT); el += (uint16_t)(n - s); s = n;
        if (el > 3000000u) { noSync++; return (uint16_t)(top + 1); }
    }
}

/* one pass: every line point of set-up s at place k with block size b; keep = 0 for the warm-up */
static void pass(int s, int b, int k, int keep) {
    const int bs = BS[b];
    const uint16_t p = (uint16_t)(STEP * k);
    const uint32_t lo0 = 0x1000u + (uint32_t)b * 0x100u;
    if (noSync > 4) return;                                 /* Timer 1 neither resets at VBlank nor ticks: give up, quickly */
    frameStart();
    for (int j = 0; j < nPts[s] && noSync <= 4; j++) {
        const uint16_t L = (uint16_t)(2 + 4 * j);
        waitIdle();
        gp1(0x01000000u);
        gp0(0x02000000u); gp0((RY << 16) | RX); gp0((4u << 16) | 64u);           /* the rectangle cleared: 0000h */
        waitIdle();
        gp0(0xA0000000u); gp0((RY << 16) | RX); gp0((4u << 16) | 64u);
        gp1(0x04000002u);                                                         /* DMA direction CPU -> GP0 */
        write32(D2_MADR, (uint32_t)pat[b] & 0x1FFFFF);
        write32(D2_BCR, ((uint32_t)(128 / bs) << 16) | (uint32_t)bs);
        if (read16(T1_COUNT) >= L) { frameStart(); late[s][b] += keep; }   /* the point before ran past this line: the next frame's */
        uint16_t wrong, zero, tt;
        uint32_t left = 0;
        {
            for (uint32_t i = 0; read16(T1_COUNT) < L; i++) if (i > 2000000u) { noSync++; break; }
            const uint16_t s0 = read16(T2_COUNT);
            while ((uint16_t)(read16(T2_COUNT) - s0) < p) {}
            const uint16_t t0 = read16(T2_COUNT);
            write32(D2_CHCR, 0x01000201u);                                        /* to the GPU, SyncMode 1, start */
            uint32_t polls = 0;
            while ((read32(D2_CHCR) & 0x01000000u) && polls < 100000) polls++;
            tt = (uint16_t)(read16(T2_COUNT) - t0);
            if (polls >= 100000) write32(D2_CHCR, 0);
            waitT2(1000);
            while (!(gstat() & (1u << 26)) && left < 255) { gp0(0); left++; for (int i = 0; i < 4 && !(gstat() & (1u << 26)); i++) {} }
            gp1(0x01000000u);
            gp1(0x04000000u);
            waitIdle();
            gp0(0xC0000000u); gp0((RY << 16) | RX); gp0((4u << 16) | 64u);
            for (int i = 0; i < 400000 && !(gstat() & (1u << 27)); i++) {}
            uint32_t bad = 0, z = 0, lo = lo0;
            for (int i = 0; i < 128; i++) {
                const uint32_t v = read32(GP0);
                const uint32_t x = v ^ (lo | ((lo + 1) << 16));
                if (x & 0xFFFFu) { bad++; if (!(v & 0xFFFFu)) z++; }
                if (x >> 16) { bad++; if (!(v >> 16)) z++; }
                lo += 2;
            }
            wrong = (uint16_t)bad; zero = (uint16_t)z;
        }
        if (keep) { rWrong[s][b][k][j] = wrong; rZero[s][b][k][j] = zero; rLeft[s][b][k][j] = (uint8_t)left; rTime[s][b][k][j] = tt; }
    }
}

static void display(int s, uint32_t pal) {
    if (s == 0) return;
    gp1(0x05000000u);                                       /* display area 0,0 */
    gp1(0x06000000u | 0x200u | (0xC00u << 12));
    gp1(0x07000000u | 0x10u | (0x100u << 10));
    gp1(0x08000000u | (s == 2 ? 0x27u : 0x01u) | (pal << 3));
    gp1(0x03000000u | (s == 3 ? 1u : 0u));
}

/* a histogram of one result over a set-up and block size: "value x points", most points first, up to 12 values */
static void histo(const char* name, int s, int b, int which) {
    static uint16_t val[64]; static uint32_t cnt[64];
    int n = 0, over = 0;
    for (int k = 0; k < NK; k++)
        for (int j = 0; j < nPts[s]; j++) {
            const uint16_t v = which == 0 ? rWrong[s][b][k][j] : which == 1 ? rZero[s][b][k][j] : rLeft[s][b][k][j];
            int i = 0;
            while (i < n && val[i] != v) i++;
            if (i == n) { if (n == 64) { over++; continue; } val[n] = v; cnt[n] = 0; n++; }
            cnt[i]++;
        }
    printf("  %s:", name);
    for (int shown = 0; shown < 12 && shown < n; shown++) {
        int m = shown;
        for (int i = shown + 1; i < n; i++) if (cnt[i] > cnt[m] || (cnt[i] == cnt[m] && val[i] < val[m])) m = i;
        const uint16_t tv = val[m]; const uint32_t tc = cnt[m];
        val[m] = val[shown]; cnt[m] = cnt[shown]; val[shown] = tv; cnt[shown] = tc;
        printf(" %d x %d", (int)tv, (int)tc);
    }
    if (n > 12 || over) printf(" (and %d more values)", n - 12 + over);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-dma-map\n");
    printf("A 64x4 upload by DMA2 in blocks of 8, 16, 32, 64 words at every 4th line x 64 places along it, four displays. No assertions.\n");
    for (int b = 0; b < NB; b++)
        for (int i = 0; i < 128; i++) pat[b][i] = (uint32_t)(0x1000 + b * 0x100 + 2 * i) | ((uint32_t)(0x1001 + b * 0x100 + 2 * i) << 16);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0);                                    /* system clock, free */
    write16(T1_MODE, 0x0103u);                              /* hblank clock, sync on, mode 1: reset at VBlank */
    dpcrWas = read32(DPCR);
    write32(DPCR, dpcrWas | 0x00000800u);                   /* DMA2 on */
    ramSize = read32(RAM_SIZE);
    for (int i = 0; i < 5; i++) entryDelay[i] = read32(DELAY_REGS + 4u * (uint32_t)i);
    write32(0x1F801018u, 0x00020843u); write32(0x1F801020u, 0x00031125u);   /* CDROM_DELAY, COM_DELAY: a boot without a disc */
    entryStat = gstat();
    const uint32_t pal = (entryStat >> 20) & 1;
    waitIdle();
    gp1(0x10000007u); for (int i = 0; i < 64; i++) __asm__ volatile("nop"); gpuType = read32(GP0);
    gp1(0x10000003u); for (int i = 0; i < 64; i++) __asm__ volatile("nop"); area3was = read32(GP0);
    gp0(0xE3000000u | (0x3FFu << 10) | 0x3FFu);
    waitIdle();
    gp1(0x10000003u); for (int i = 0; i < 64; i++) __asm__ volatile("nop"); area3 = read32(GP0);
    gp0(0xE3000000u | (area3was & 0xFFFFFu));
    gp0(0x03000000u); gp0(0xE6000000u);
    vramRead((int)RX, (int)RY, 64, 4, saved);

    for (int s = 0; s < NS; s++) {
        display(s, pal);
        for (int f = 0; f < 3; f++) frameStart();
        frames2[s][0] = frameLines(); frames2[s][1] = frameLines();
        lines[s] = frames2[s][0] < frames2[s][1] ? frames2[s][0] : frames2[s][1];
        int n = 0;
        while (n < NL && 2 + 4 * n + 5 < lines[s]) n++;
        nPts[s] = (uint16_t)n;
        pass(s, NB - 1, 0, 0);                              /* the warm-up, thrown away */
        for (int b = 0; b < NB; b++)
            for (int k = 0; k < NK; k++) pass(s, b, k, 1);
    }

    gp1(0x08000000u | ((entryStat >> 17) & 3) | (((entryStat >> 19) & 1) << 2) | (((entryStat >> 20) & 1) << 3) |
        (((entryStat >> 21) & 1) << 4) | (((entryStat >> 22) & 1) << 5) | (((entryStat >> 16) & 1) << 6) | (((entryStat >> 14) & 1) << 7));
    gp1(0x03000000u | ((entryStat >> 23) & 1));
    vramWrite((int)RX, (int)RY, 64, 4, saved);
    write32(DPCR, dpcrWas);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    for (int i = 0; i < 5; i++) write32(DELAY_REGS + 4u * (uint32_t)i, entryDelay[i]);
    ExitCriticalSection();

    static const char* NAME[NS] = { "as found", "320x240", "640x480i", "off" };
    printf("\nGPUSTAT at entry %08x, GP1(10h:07h) %08x, E3h = 3FF,3FF read through 03h %08x; RAM_SIZE %08x, DPCR %08x\n",
           (unsigned)entryStat, (unsigned)gpuType, (unsigned)area3, (unsigned)ramSize, (unsigned)dpcrWas);
    printf("Delay registers at entry (1F801010h BIOS, SPU, CD-ROM, Exp 2, COM_DELAY): %08x %08x %08x %08x %08x; pinned CD-ROM 00020843, COM 00031125\n",
           (unsigned)entryDelay[0], (unsigned)entryDelay[1], (unsigned)entryDelay[2], (unsigned)entryDelay[3], (unsigned)entryDelay[4]);
    printf("Timer 1 lines a frame (two frames) per set-up:");
    for (int s = 0; s < NS; s++) printf(" %s %d %d", NAME[s], (int)frames2[s][0], (int)frames2[s][1]);
    printf("; VBlank waits timed out %d\n", (int)noSync);
    static char row[NK + 1];
    for (int s = 0; s < NS; s++)
        for (int b = 0; b < NB; b++) {
            int pts = 0, lost = 0; uint32_t tmin = 0xFFFF, tmax = 0;
            for (int k = 0; k < NK; k++)
                for (int j = 0; j < nPts[s]; j++) {
                    pts++;
                    if (rWrong[s][b][k][j]) lost++;
                    const uint32_t t = rTime[s][b][k][j];
                    if (t < tmin) tmin = t;
                    if (t > tmax) tmax = t;
                }
            printf("\nMAP %d %s bs %d: %d lines x %d places, %d points (%d waited a frame): %d lost data\n", s, NAME[s], BS[b], (int)nPts[s],
                   NK, pts, (int)late[s][b], lost);
            histo("halfwords wrong", s, b, 0);
            histo("halfwords read 0", s, b, 1);
            histo("words still wanted", s, b, 2);
            printf("  transfer cycles %d - %d\n", (int)tmin, (int)tmax);
            if (!lost) continue;
            printf("  line  places 0..63, 34 cycles apart\n");
            for (int j = 0; j < nPts[s]; j++) {
                for (int k = 0; k < NK; k++) {
                    const uint16_t w = rWrong[s][b][k][j];
                    row[k] = w == 0 ? '.' : (char)('G' + (w - 1) / 16);
                }
                row[NK] = 0;
                printf("  %4d  %s\n", 2 + 4 * j, row);
            }
        }
    printf("\n");
    for (int s = 0; s < NS; s++) {
        printf("SUMMARY %d %s: points that lost data at blocks of 8 16 32 64:", s, NAME[s]);
        for (int b = 0; b < NB; b++) {
            int lost = 0;
            for (int k = 0; k < NK; k++) for (int j = 0; j < nPts[s]; j++) if (rWrong[s][b][k][j]) lost++;
            printf(" %d", lost);
        }
        printf(" of %d each\n", (int)nPts[s] * NK);
    }
    printf("\ngpu/gpu-dma-map: Done.\n");
    __asm__ volatile("break 4, 0");                        /* psxmon ends a run here; our runner stops at the Done line */
    for (;;) VSync(0);
}
