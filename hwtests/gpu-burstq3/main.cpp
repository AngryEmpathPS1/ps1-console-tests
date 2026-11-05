/*
 * gpu/gpu-burstq3 - does a burst that overran the GPU's FIFO slow the next upload? (2026-11-05)
 *
 * WHY. The same 4-word upload (1,024 words, a 256-wide rectangle at (0,256), CHCR2 polled) takes the second SCPH-5501 3,880 cycles in
 * hwtests/gpu-burstline and ~3,670 in hwtests/gpu-burstq and gpu-burstq2 - where neither a GP0(02h) fill of the rectangle nor a VRAM
 * read-back before it moved it (this emulator ~3,670 in all). What gpu-burstline does that they do not: each 4-word upload comes right
 * after a BURST (sync 0) of the same words to the same place at the same d, which overruns GP0's FIFO and loses words, the CPU then
 * writing zeros until GPUSTAT bit 26 sets. Does that overrun leave the GPU slower for the next upload?
 *
 * WHAT IT DOES. gpu-burstq2's upload, after condition c: 0 nothing, 4 a burst first (gpu-burstline's: the fill, GP0(A0h), 1,024 words
 * by sync 0 started at the same d, zeros until bit 26, GP1(04h) 0), 5 a burst and then a fill, 7 a burst, a read-back and a fill
 * (gpu-burstline's sequence). Polled (P) and quiet (Q) as gpu-burstq2; d 0..2,048 by 256. A line: C c d P t Q W.
 *
 * READ IT LIKE THIS. P and Q by c.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstq3 --frames=8000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 1024

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static uint16_t back[WORDS * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* registers only: n turns of a 2-instruction loop (addiu, bnez - the delay slot a nop), ~2 cycles a turn in the I-cache */
static void spin(uint32_t n) {
    if (!n) return;
    __asm__ volatile(".set noreorder\n1: addiu %0, %0, -1\n bnez %0, 1b\n nop\n.set reorder" : "+r"(n) : : "memory");
}

/* start the upload d cycles after a line edge; returns with CHCR written (the caller times or waits) */
static void lineThen(uint32_t d) {
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
}
static void start(uint32_t d, int c) {
    const int w = 256, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    if (c & 4) {   /* a burst first, as gpu-burstline's measurement before (overruns the FIFO, loses words) */
        write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
        gpuIdle();
        write32(GP1, 0x04000002u);
        write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
        waitT2(2000);
        write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
        write32(BCR2, (uint32_t)WORDS);
        lineThen(d);
        write32(CHCR2, 0x11000001u);
        for (int i = 0; i < 200000 && (read32(CHCR2) & 0x01000000u); i++) {}
        waitT2(3000);
        for (int z = 0; z < 4096 && !(read32(GP1) & (1u << 26)); z++) { write32(GP0, 0); for (volatile int i = 0; i < 8; i++) {} }
        write32(GP1, 0x04000000u);
        gpuIdle();
        write32(GP1, 0x01000000u);
    }
    if (c & 2) {   /* a read-back of the rectangle first (as gpu-burstline's measurement before) */
        write32(GP1, 0x04000000u); gpuIdle();
        write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
        for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
        for (int k = 0; k < WORDS; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
        gpuIdle(); write32(GP1, 0x01000000u);
    }
    if (c & 1) { write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w); gpuIdle(); }   /* the fill gpu-burstline makes */
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / 4) << 16) | 4u);
    lineThen(d);
}
static void finish() {
    for (int i = 0; i < 400000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
}

int main() {
    printf("\ngpu/gpu-burstq3\n");
    printf("C c d P t Q W (c: 0 nothing first, 4 a burst, 5 a burst then a fill, 7 a burst, a read-back, a fill; P polled, Q quiet). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    /* the spin's cycles a turn, measured here (Timer 2): turns for a W */
    const uint16_t s0 = read16(T2_COUNT); spin(10000); const uint16_t s1 = read16(T2_COUNT);
    const uint32_t per100 = (uint32_t)(uint16_t)(s1 - s0) / 100u;   /* cycles a 100 turns */
    printf("spin: %u cycles a 100 turns\n", (unsigned)per100);
    static const int CS[4] = { 0, 4, 5, 7 };
    for (int ci = 0; ci < 4; ci++) for (uint32_t d = 0; d <= 2048; d += 256) {
        const int c = CS[ci];
        start(d, c);
        const uint16_t t0 = read16(T2_COUNT);
        write32(CHCR2, 0x01000201u);
        uint32_t polls = 0;
        while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
        const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
        finish();
        uint32_t lo = 3000, hi = 4600;
        while (hi - lo > 4) {
            const uint32_t mid = (lo + hi) / 2;
            start(d, c);
            write32(CHCR2, 0x01000201u);
            spin(mid * 100u / per100);
            const int busy = (read32(CHCR2) & 0x01000000u) != 0;
            finish();
            if (busy) lo = mid; else hi = mid;
        }
        printf("C %d %u P %u Q %u\n", c, (unsigned)d, (unsigned)t, (unsigned)hi);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-burstq3: Done.\n");
    for (;;) VSync(0);
}
