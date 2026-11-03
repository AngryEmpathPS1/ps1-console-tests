/*
 * gpu/gpu-dma-quiet - a GPU-busy DMA upload timed with the CPU off the bus (2026-11-03 night, for the SCPH-5501).
 *
 * WHY. ps1-tests' dma/chopping times each SyncMode 1 upload by polling CHCR2, and this core's GPU-busy rows (blocks of 17-128
 * words, the GPU's FIFO full) follow that poll: its intake takes words at the CPU's clock when its scheduler event runs late
 * (v0.377.264's KNOWN-OPEN at GPU.feed). With the events on their own time (experiments/_mkfnow.js) those rows read 1.25% fast and
 * no intake setting fits them and gpu/bandwidth together. A console's DMA does not care where the CPU's poll is, except for the
 * bus cycles the poll's reads take. So this times the same uploads both ways: by chopping's own poll, and QUIET - the CPU
 * waits in cached code (no bus access at all) for a set time and then reads CHCR2 once. The quiet end, scanned in 8-cycle steps,
 * is the transfer's own length; the poll's against it is what the poll costs.
 *
 * WHAT IT DOES. 64 x 64 16-bit pixels (2,048 words) by GP0(A0h), GP1(04h) = 2, SyncMode 1 at block sizes 4, 16, 17, 24, 32, 48,
 * 64 and 128 (chopping's set-up: GP1(01h) and the GPU idle first). Each try starts a fixed offset after an hblank edge (Timer 1
 * counting hblanks), 16 offsets 0..2,047 apart, interrupts off. POLL: CHCR2 read in a loop, Timer 2 after it. QUIET: from 400
 * under the poll's fewest, 8 cycles more each time, a cached spin and then Timer 2 and one CHCR2 read, until it reads done. Per
 * block size: the poll's fewest, mean and most; the quiet first-done's fewest, mean and most, and the last not-done's mean.
 *
 * READ IT LIKE THIS. Each machine's table against the other's. The quiet columns are the upload's length within 8 cycles; poll
 * minus quiet is the poll's own cost. No assertions. It prints only between block sizes, so it needs a long quiet budget:
 * `node hwtests/run.js gpu-dma-quiet --ntsc --frames=6000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* n turns of a cached three-instruction loop: no bus access */
static inline void spin(uint32_t n) {
    __asm__ volatile(".set push\n.set noreorder\n1: addiu %0, %0, -1\nbnez %0, 1b\nnop\n.set pop\n" : "+r"(n));
}

static uint32_t buf[2048] __attribute__((aligned(16)));
static uint32_t bsNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer */
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 512u); write32(GP0, (64u << 16) | 64u);
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, ((2048u / bsNow) << 16) | bsNow);
}
/* start at `off` cycles after an hblank edge */
static void atLine(uint32_t off) {
    const uint16_t h = read16(T1_COUNT);
    for (unsigned k = 0; k < 100000 && read16(T1_COUNT) == h; k++) {}
    waitCycles(off);
}
static __attribute__((noinline, noclone)) uint16_t pollOne() {
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    uint16_t a2; unsigned turns = 0;
    for (;;) { a2 = read16(T2_COUNT); if (!(read32(CHCR2) & 0x01000000u)) break; if (++turns > 20000) break; }
    return (uint16_t)(a2 - a0);
}
static __attribute__((noinline, noclone)) uint16_t quietOne(uint32_t n, bool* done) {
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(n);
    const uint16_t a1 = read16(T2_COUNT);
    *done = !(read32(CHCR2) & 0x01000000u);
    return (uint16_t)(a1 - a0);
}

static const uint32_t SIZES[8] = { 4, 16, 17, 24, 32, 48, 64, 128 };
#define TRIES 16

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-quiet\n");
    printf("2,048 words by SyncMode 1 GPU DMA blocks, timed by a CHCR2 poll and QUIET (a cached wait, then one read). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);   /* Timer 1 counts hblanks */
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 2048; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; setup(); pollOne(); settle(); setup(); { bool d; quietOne(10, &d); } settle();   /* cache them */
    printf(" bs  blocks   poll: fewest   mean   most    quiet done: fewest   mean   most   not done: mean   lost\n");
    for (int s = 0; s < 8; s++) {
        bsNow = SIZES[s];
        uint32_t pLo = 0xFFFF, pHi = 0, pSum = 0, qLo = 0xFFFF, qHi = 0, qSum = 0, nSum = 0, qN = 0, lost = 0;
        for (int t = 0; t < TRIES; t++) {
            const uint32_t off = 40u + (uint32_t)t * 131u;
            setup(); atLine(off);
            const uint16_t p = pollOne(); settle();
            if (p < pLo) pLo = p; if (p > pHi) pHi = p; pSum += p;
        }
        for (int t = 0; t < TRIES; t++) {
            const uint32_t off = 40u + (uint32_t)t * 131u;
            uint32_t want = pLo > 400 ? pLo - 400 : 0, last = 0;
            bool found = false;
            for (int k = 0; k < 120 && !found; k++, want += 8) {
                bool d = false;
                setup(); atLine(off);
                const uint16_t e = quietOne(want / 3 + 1, &d); settle();
                if (d) { found = true; if (e < qLo) qLo = e; if (e > qHi) qHi = e; qSum += e; nSum += last; qN++; }
                else last = e;
            }
            if (!found) lost++;
        }
        const unsigned pm = (unsigned)(pSum * 10 / TRIES), qm = qN ? (unsigned)(qSum * 10 / qN) : 0, nm = qN ? (unsigned)(nSum * 10 / qN) : 0;
        printf("%3u  %5u        %5u %5u.%u  %5u              %5u %5u.%u  %5u            %5u.%u  %5u\n", (unsigned)bsNow, (unsigned)(2048 / bsNow),
               (unsigned)pLo, pm / 10, pm % 10, (unsigned)pHi, (unsigned)qLo, qm / 10, qm % 10, (unsigned)qHi, nm / 10, nm % 10, (unsigned)lost);
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-quiet: Done.\n");
    for (;;) {}   /* park after Done. */
}
