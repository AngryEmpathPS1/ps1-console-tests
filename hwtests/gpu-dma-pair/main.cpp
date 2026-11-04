/*
 * gpu/gpu-dma-pair - two CPU reads against a GPU upload's blocks, read by read: three Timer 2 reads, the first N single cycles into a
 * long SyncMode 1 upload, the second k nops after it, the third ~40 cycles after that (2026-11-04, for the SCPH-5501). Written by
 * experiments/mkdmapair.js.
 *
 * WHY. hwtests/gpu-a0poll on the second SCPH-5501: two adjacent on-die reads a turn slow 4-, 8- and 16-word uploads 0.8-1.4 cycles a
 * block, a lone read late in a gap drags the blocks, and reads the loop's period puts inside the blocks cost nothing; v0.377.272 gives a
 * pair's second read the bus from a block due as it issues (DMA_IO_PAIR_TIE) and the block a cycle after its start (DMA_IO_PAIR_HOLD).
 * But dma-arb's IOR rows (48 back-to-back DPCR reads a turn) end ~450 cycles later on every console than on this emulator, where each
 * read is held by a block and its next one issues just past the next block's due cycle; and gpu-a0poll's lone reads in loops slower
 * than the blocks run free on the console and are held here. This places a pair at every cycle of a block's period, k apart, and
 * reads where each was served and, with the third, where the blocks after them went. Mode U uses the first value at once (the CPU must
 * wait for it); mode F never does (does the CPU run past a held read?).
 *
 * WHAT IT DOES. gpu-dma-gaps2's set-up and start (the DRAM refresh's grid, four phases, 8 tries a value): blocks of 4 (72, the reads
 * 640 + N cycles after CHCR2's store) and of 16 (24, 400 + N), into one row exactly the upload long (no row start inside), N = 0..47; k = 0, 1, 2, 3,
 * 4, 5, 6, 8; modes F and U.
 *
 * READ IT LIKE THIS. One line a cell and phase: B mode k P (off), then for N = 0..47 the three reads' values (the value most of 8
 * tries read) as a1/a2/a3, each after the store. No assertions.
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
    /* one row exactly the words long (576 or 768 pixels): no row start inside, and the GPU ready again at the end - a 1,024-wide row left
     * it waiting for the rest, and settle()'s bounded wait ran out after every transfer (the first build: 5 minutes, cut off) */
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (1u << 16) | (words * 2));
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}
struct Three { uint16_t a1, a2, a3; };
static __attribute__((noinline, noclone)) Three one(uint32_t n, uint32_t p, uint32_t sel) {
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(wNow / 3);
    sled(n);
    uint32_t r1, r2, r3;
    switch (sel) {
        case 0: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 1: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 2: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 3: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 4: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 5: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 6: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 8: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 16: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 17: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 18: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 19: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 20: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 21: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 22: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        case 24: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lhu   %0, 0x1120($t0)\n"
            "nop\n"
            "addu  $t1, %0, $zero\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "li    $t1, 13\n"
            "1: addiu $t1, $t1, -1\n"
            "bnez  $t1, 1b\n"
            "nop\n"
            "lhu   %2, 0x1120($t0)\n"
            "nop\n"
            ".set pop\n" : "=&r"(r1), "=&r"(r2), "=&r"(r3) :: "t0", "t1", "memory"); break;
        default: r1 = r2 = r3 = 0;
    }
    Three t; t.a1 = (uint16_t)(r1 - a0); t.a2 = (uint16_t)(r2 - a0); t.a3 = (uint16_t)(r3 - a0); return t;
}

#define NMAX 48
static const uint32_t KS[8] = { 0, 1, 2, 3, 4, 5, 6, 8 };
static Three vals[NMAX][8], best[NMAX];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-pair\n");
    printf("Three Timer 2 reads in a long SyncMode 1 upload: the first W + N cycles after CHCR2's store, the second k nops on, the third ~40 cycles after that; a1/a2/a3 by N, the value most of 8 read. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 1024; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; kNow = 72; wNow = 640;
    for (uint32_t s = 0; s < 32; s++) { if ((s & 15) >= 8) continue; setup(); one(0, 0, s); settle(); }   /* cache them */
    static const uint16_t BS[2] = { 4, 16 }, KB[2] = { 72, 24 }, WB[2] = { 640, 400 };
    for (int b = 0; b < 2; b++) for (int use = 0; use < 2; use++) for (int ki = 0; ki < 8; ki++) {
        bsNow = BS[b]; kNow = KB[b]; wNow = WB[b];
        const uint32_t sel = (uint32_t)use * 16 + KS[ki];
        for (uint32_t p = 0; p < 256; p += 64) {
            for (int t = 0; t < 8; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = one((uint32_t)n, p, sel);
                settle();
            }
            unsigned off = 0;
            for (int n = 0; n < NMAX; n++) {
                int bi = 0, bn = 0;
                for (int i = 0; i < 8; i++) { int c2 = 0; for (int j = 0; j < 8; j++) c2 += vals[n][j].a1 == vals[n][i].a1 && vals[n][j].a2 == vals[n][i].a2 && vals[n][j].a3 == vals[n][i].a3; if (c2 > bn) { bi = i; bn = c2; } }
                best[n] = vals[n][bi]; off += 8 - bn;
            }
            printf("%u %c %u %u (off %u):", (unsigned)bsNow, use ? 'U' : 'F', (unsigned)KS[ki], (unsigned)p, off);
            for (int n = 0; n < NMAX; n++) printf(" %u/%u/%u", (unsigned)best[n].a1, (unsigned)best[n].a2, (unsigned)best[n].a3);
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-pair: Done.\n");
    for (;;) {}   /* park after Done. */
}
