/*
 * gpu/gpu-dma-hold - which register reads a GPU upload's blocks hold, and for how much of a block's period: one read of Timer 2, CHCR2,
 * DPCR, I_STAT, GPUSTAT or a RAM word N single cycles into a long SyncMode 1 upload, timed by a Timer 2 read right behind it
 * (2026-11-04, for the SCPH-5501). Written by experiments/mkdmahold.js.
 *
 * WHY. On the second SCPH-5501, hwtests/gpu-dma-pair: a Timer 2 read is held for 11 of a 4-word block's 14 cycles, and the second of
 * two back to back reads the first's +4 at every phase (never held itself). But hwtests/gpu-a0poll's loop of one CHCR2 read a turn
 * runs at its own period (10 + n cycles) once it is slower than the blocks, where a read held 11 cycles in 14 would quantise it to the
 * blocks - as this emulator does. So the registers may be held differently: this reads each against the blocks.
 *
 * WHAT IT DOES. gpu-dma-pair's set-up and start (one row exactly the upload long, the DRAM refresh's grid at four phases, 8 tries a
 * value): blocks of 4 (72, the read 640 + N cycles after CHCR2's store) and of 16 (24, 400 + N), N = 0..47; then the register, then
 * Timer 2 right behind it.
 *
 * READ IT LIKE THIS. One line a cell and phase: B register P (off), then for N = 0..47 the Timer 2 value behind the read (the value
 * most of 8 tries read), after the store - flats are the read held, rises the read through. No assertions.
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

#define SLED 192
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 192\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n < SLED ? n : SLED - 1) : "t0", "t1");
}
static inline void spin(uint32_t n) {
    __asm__ volatile(".set push\n.set noreorder\n1: addiu %0, %0, -1\nbnez %0, 1b\nnop\n.set pop\n" : "+r"(n));
}

static uint32_t buf[1024] __attribute__((aligned(16)));
static volatile uint32_t ramWord;
static uint32_t bsNow, kNow, wNow;
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);
    settle();
    const uint32_t words = kNow * bsNow;
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (1u << 16) | (words * 2));   /* one row exactly the upload long */
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}
static __attribute__((noinline, noclone)) uint16_t one(uint32_t n, uint32_t p, uint32_t sel) {
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(wNow / 3);
    sled(n);
    uint32_t r1, r2;
    switch (sel) {
        case 0: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        case 1: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lw   %0, 0x10a8($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        case 2: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lw   %0, 0x10f0($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        case 3: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lw   %0, 0x1070($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        case 4: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lw   %0, 0x1814($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        case 5: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lw   %0, 0(%2)\n"
            "lhu   %1, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2) : "r"(&ramWord) : "t0", "memory"); break;
        default: r1 = r2 = 0;
    }
    (void)r1;
    return (uint16_t)(r2 - a0);
}

#define NMAX 48
static const char* const NAMES[6] = { "T2", "CHCR2", "DPCR", "ISTAT", "GPUSTAT", "RAM" };
static uint16_t vals[NMAX][8], best[NMAX];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-hold\n");
    printf("One read of a register W + N cycles after CHCR2's store in a long SyncMode 1 upload, Timer 2 read right behind it: the value most of 8 read by N. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 1024; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; kNow = 72; wNow = 640;
    for (uint32_t s = 0; s < 6; s++) { setup(); one(0, 0, s); settle(); }   /* cache them */
    static const uint16_t BS[2] = { 4, 16 }, KB[2] = { 72, 24 }, WB[2] = { 640, 400 };
    for (int b = 0; b < 2; b++) for (uint32_t r = 0; r < 6; r++) {
        bsNow = BS[b]; kNow = KB[b]; wNow = WB[b];
        for (uint32_t p = 0; p < 256; p += 64) {
            for (int t = 0; t < 8; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = one((uint32_t)n, p, r);
                settle();
            }
            unsigned off = 0;
            for (int n = 0; n < NMAX; n++) {
                int bi = 0, bn = 0;
                for (int i = 0; i < 8; i++) { int c2 = 0; for (int j = 0; j < 8; j++) c2 += vals[n][j] == vals[n][i]; if (c2 > bn) { bi = i; bn = c2; } }
                best[n] = vals[n][bi]; off += 8 - bn;
            }
            printf("%u %s %u (off %u):", (unsigned)bsNow, NAMES[r], (unsigned)p, off);
            for (int n = 0; n < NMAX; n++) printf(" %u", (unsigned)best[n]);
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-hold: Done.\n");
    for (;;) {}   /* park after Done. */
}
