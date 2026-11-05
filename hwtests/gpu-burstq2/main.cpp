/*
 * gpu/gpu-burstq2 - what before a 4-word upload slows it on the console: a GP0(02h) fill of its rectangle, a VRAM read-back, both, neither (2026-11-05)
 *
 * WHY. The same 4-word upload (1,024 words, a 256-wide rectangle at (0,256), CHCR2 polled) takes the second SCPH-5501 3,880 cycles in
 * hwtests/gpu-burstline and 3,660-3,684 in hwtests/gpu-burstq (this emulator ~3,675 in both). gpu-burstline fills the rectangle with
 * GP0(02h) before each upload and reads it back through GP0(C0h) / GPUREAD after; gpu-burstq does neither. Which one slows the next
 * upload by ~0.8 cycles a block?
 *
 * WHAT IT DOES. gpu-burstq's upload (interrupts off, Timer 2 at the system clock, 4-word blocks, started d cycles after a GPUSTAT bit-31
 * change, d 0..2,048 by 256) after condition c: 0 nothing, 1 a GP0(02h) fill of the rectangle, 2 a read-back of the rectangle
 * (GP0(C0h), 1,024 GPUREADs), 3 both (the read-back, then the fill: gpu-burstline's order across two measurements). Each timed
 * polled (P: CHCR2 read to the end) and quiet (Q: the CPU spinning in registers W cycles, one CHCR2 read, W found by halving).
 * A line: C c d P t Q W.
 *
 * READ IT LIKE THIS. P and Q by c: the condition that slows them is what this emulator lacks.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstq2 --frames=6000 --quiet=1500`.
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
static void start(uint32_t d, int c) {
    const int w = 256, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
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
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
}
static void finish() {
    for (int i = 0; i < 400000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
}

int main() {
    printf("\ngpu/gpu-burstq2\n");
    printf("C c d P t Q W (c: 1 a fill of the rectangle first, 2 a read-back first, 3 both, 0 neither; P polled, Q quiet). No assertions.\n\n");
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
    for (int c = 0; c < 4; c++) for (uint32_t d = 0; d <= 2048; d += 256) {
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
    printf("\ngpu/gpu-burstq2: Done.\n");
    for (;;) VSync(0);
}
