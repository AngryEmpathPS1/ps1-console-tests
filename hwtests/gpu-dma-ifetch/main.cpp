/*
 * gpu/gpu-dma-ifetch - instruction-cache refills during a GPU upload (2026-11-04, for the SCPH-5501). Written by experiments/mkdmafetch.js.
 *
 * WHY. hwtests/gpu-dma-pair on the second SCPH-5501: in its mode U k = 4-6 cells - code the test's 14 KB lays across the 4 KB I-cache -
 * the console's second read comes 16-48 cycles later than this emulator's, at the console's own refresh phase (experiments/_mkt2ref.js),
 * where every cell whose code is warm reads as the console. So a refill during an upload costs the console more than it costs here, and
 * every game fetches code while a GPU DMA runs. This times refills against the blocks with nothing else in the way.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock (restarted here). First the DRAM refresh's place against Timer 2: one RAM load
 * timed at each of 256 phases (two passes, the second kept) - printed as the phase of the longest and the table. Then one 4 KB page of asm
 * (everything between the eviction and the second read runs from it): two Timer 2 reads 0x40 bytes apart, L0 (the first) .. L4 (the
 * second), the last M of the three lines L1-L3 between them made cold by an evictor 4 KB above (M = 0-3); the whole sequence run once dry
 * first (CHCR2 written with 0: every line it uses warm), then the eviction, then the sync to Timer 2 phase p, a sled, CHCR2's store (a SyncMode 1 upload from RAM of B-word blocks into one row exactly its length,
 * as gpu-dma-pair's: B 4 x 72, the first read ~650 + N cycles after the store; B 16 x 24, ~400 + N; or B 0, no upload), a spin, a sled of
 * N single cycles, and the timed code. N = 0..47, p = 0, 64, 128, 192, 8 tries a value.
 *
 * READ IT LIKE THIS. One line a cell and phase: B M p (off), then for N = 0..47 the two reads' values after the store (the value most of
 * 8 tries read) as a1/a2. a2 - a1 is the code's own time: 16 instructions and M refills; against B 0 the blocks' price on them. No
 * assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".global ift\n"
    "ift:\n"
    "ift_L0:\n"
    "lhu $v0, 0x1120($t0)\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_L1:\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_L2:\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_L3:\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_L4:\n"
    "lhu $v1, 0x1120($t0)\n"
    "nop\n"
    "jr $t6\n"
    "nop\n"
    ".balign 256\n"
    ".global ift_go\n"
    "ift_go:\n"
    "addiu $sp, $sp, -16\n"
    "sw $ra, 0($sp)\n"
    "lui $t0, 0x1f80\n"
    "andi $t8, $a0, 0xff\n"
    "srl $a0, $a0, 8\n"
    "andi $a0, $a0, 0xff\n"
    "andi $t7, $a1, 0xff\n"
    "srl $a3, $a1, 8\n"
    "la $t9, 1f\n"
    "la $t1, ift_sled\n"
    "jr $t1\n"
    "nop\n"
    "1:\n"
    "move $at, $zero\n"
    "ift_pass:\n"
    "4:\n"
    "lhu $t2, 0x1120($t0)\n"
    "nop\n"
    "subu $t3, $a0, $t2\n"
    "andi $t3, $t3, 0xff\n"
    "sltiu $t4, $t3, 70\n"
    "beqz $t4, 4b\n"
    "nop\n"
    "sltiu $t4, $t3, 20\n"
    "bnez $t4, 4b\n"
    "nop\n"
    "lhu $t2, 0x1120($t0)\n"
    "nop\n"
    "subu $t3, $a0, $t2\n"
    "addiu $t3, $t3, -6\n"
    "andi $t3, $t3, 0xff\n"
    "sltiu $t4, $t3, 192\n"
    "bnez $t4, 5f\n"
    "nop\n"
    "li $t3, 191\n"
    "5:\n"
    "la $t9, 6f\n"
    "la $t1, ift_sled_end\n"
    "sll $t3, $t3, 2\n"
    "subu $t1, $t1, $t3\n"
    "jr $t1\n"
    "nop\n"
    "6:\n"
    "lhu $t5, 0x1120($t0)\n"
    "subu $t1, $zero, $at\n"
    "and $t1, $t1, $a2\n"
    "sw $t1, 0x10a8($t0)\n"
    "move $a1, $a3\n"
    "7:\n"
    "addiu $a1, $a1, -1\n"
    "bnez $a1, 7b\n"
    "nop\n"
    "la $t6, ift_end\n"
    "la $t9, ift_L0\n"
    "la $t1, ift_sled_end\n"
    "sll $t3, $t8, 2\n"
    "subu $t1, $t1, $t3\n"
    "jr $t1\n"
    "nop\n"
    "ift_end:\n"
    "bnez $at, ift_done\n"
    "nop\n"
    "beqz $t7, 3f\n"
    "nop\n"
    "jal ift_e3\n"
    "nop\n"
    "sltiu $t1, $t7, 2\n"
    "bnez $t1, 3f\n"
    "nop\n"
    "jal ift_e2\n"
    "nop\n"
    "sltiu $t1, $t7, 3\n"
    "bnez $t1, 3f\n"
    "nop\n"
    "jal ift_e1\n"
    "nop\n"
    "3:\n"
    "li $at, 1\n"
    "b ift_pass\n"
    "nop\n"
    "ift_done:\n"
    "subu $v0, $v0, $t5\n"
    "andi $v0, $v0, 0xffff\n"
    "subu $v1, $v1, $t5\n"
    "andi $v1, $v1, 0xffff\n"
    "sll $v1, $v1, 16\n"
    "or $v0, $v0, $v1\n"
    "lw $ra, 0($sp)\n"
    "addiu $sp, $sp, 16\n"
    "jr $ra\n"
    "nop\n"
    ".balign 16\n"
    ".global ift_probe\n"
    "ift_probe:\n"
    "lui $t0, 0x1f80\n"
    "srl $a0, $a0, 8\n"
    "andi $a0, $a0, 0xff\n"
    "8:\n"
    "lhu $t2, 0x1120($t0)\n"
    "nop\n"
    "subu $t3, $a0, $t2\n"
    "andi $t3, $t3, 0xff\n"
    "sltiu $t4, $t3, 70\n"
    "beqz $t4, 8b\n"
    "nop\n"
    "sltiu $t4, $t3, 20\n"
    "bnez $t4, 8b\n"
    "nop\n"
    "lhu $t2, 0x1120($t0)\n"
    "nop\n"
    "subu $t3, $a0, $t2\n"
    "addiu $t3, $t3, -6\n"
    "andi $t3, $t3, 0xff\n"
    "sltiu $t4, $t3, 192\n"
    "bnez $t4, 9f\n"
    "nop\n"
    "li $t3, 191\n"
    "9:\n"
    "la $t9, 10f\n"
    "la $t1, ift_sled_end\n"
    "sll $t3, $t3, 2\n"
    "subu $t1, $t1, $t3\n"
    "jr $t1\n"
    "nop\n"
    "10:\n"
    "lhu $t5, 0x1120($t0)\n"
    "lw $t1, 0($a1)\n"
    "lhu $t2, 0x1120($t0)\n"
    "nop\n"
    "subu $v0, $t2, $t5\n"
    "andi $v0, $v0, 0xffff\n"
    "jr $ra\n"
    "nop\n"
    ".balign 16\n"
    "ift_sled:\n"
    ".rept 192\n"
    "nop\n"
    ".endr\n"
    "ift_sled_end:\n"
    "jr $t9\n"
    "nop\n"
    ".balign 4096\n"
    "ift_ev:\n"
    ".space 0x10\n"
    "ift_e1:\n"
    "jr $ra\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_e2:\n"
    "jr $ra\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "ift_e3:\n"
    "jr $ra\n"
    "nop\n"
    "nop\n"
    "nop\n"
    ".set pop\n"
);
extern "C" uint32_t ift_go(uint32_t np, uint32_t mspin, uint32_t chcr);
extern "C" uint32_t ift_probe(uint32_t q, uint32_t addr);

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t buf[1024] __attribute__((aligned(16)));
static volatile uint32_t probeWord;
static uint32_t bsNow, kNow, wNow;
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);
    settle();
    if (!bsNow) return;   /* B 0: no upload */
    const uint32_t words = kNow * bsNow;
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (1u << 16) | (words * 2));   /* one row exactly the words long */
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}

#define NMAX 48
static uint32_t vals[NMAX][8], best[NMAX];
static uint8_t tbl[256];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-ifetch\n");
    printf("Two Timer 2 reads 0x40 bytes apart with 0-3 cold lines between them, the first ~W + N cycles into a SyncMode 1 upload of B-word blocks (B 0: none); a1/a2 by N, the value most of 8 read. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 1024; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    /* where the refresh stands against Timer 2 after its restart above */
    const uint32_t pa = ((uint32_t)&probeWord & 0x1FFFFFFFu) | 0xA0000000u;
    for (int pass = 0; pass < 2; pass++) for (uint32_t q = 0; q < 256; q++) { const uint32_t d = ift_probe(q << 8, pa); if (pass) tbl[q] = d > 255 ? 255 : (uint8_t)d; }
    uint32_t r0 = 0, lo = 255;
    for (uint32_t q = 0; q < 256; q++) { if (tbl[q] > tbl[r0]) r0 = q; if (tbl[q] < lo) lo = tbl[q]; }
    while (tbl[(r0 + 255) & 255] == tbl[r0] && r0 != 0) r0--;
    printf("refresh: a RAM load's longest at Timer 2 phase %u (%u over %u); by phase:", (unsigned)r0, (unsigned)(tbl[r0] - lo), (unsigned)lo);
    for (uint32_t q = 0; q < 256; q++) printf("%c", (char)('0' + (tbl[q] - lo > 9 ? 9 : tbl[q] - lo)));
    printf("\n");
    static const uint16_t BS[3] = { 0, 4, 16 }, KB[3] = { 0, 72, 24 }, WB[3] = { 640, 640, 400 };
    for (int b = 0; b < 3; b++) for (uint32_t m = 0; m < 4; m++) { bsNow = BS[b]; kNow = KB[b]; wNow = WB[b]; setup(); ift_go(0, m | ((wNow / 3) << 8), bsNow ? 0x01000201u : 0); settle(); }   /* everything warm */
    for (int b = 0; b < 3; b++) for (uint32_t m = 0; m < 4; m++) {
        bsNow = BS[b]; kNow = KB[b]; wNow = WB[b];
        for (uint32_t p = 0; p < 256; p += 64) {
            for (int t = 0; t < 8; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = ift_go((uint32_t)n | (p << 8), m | ((wNow / 3) << 8), bsNow ? 0x01000201u : 0);
                settle();
            }
            unsigned off = 0;
            for (int n = 0; n < NMAX; n++) {
                int bi = 0, bn = 0;
                for (int i = 0; i < 8; i++) { int c2 = 0; for (int j = 0; j < 8; j++) c2 += vals[n][j] == vals[n][i]; if (c2 > bn) { bi = i; bn = c2; } }
                best[n] = vals[n][bi]; off += 8 - bn;
            }
            printf("%u %u %u (off %u):", (unsigned)bsNow, (unsigned)m, (unsigned)p, off);
            for (int n = 0; n < NMAX; n++) printf(" %u/%u", (unsigned)(best[n] & 0xFFFFu), (unsigned)(best[n] >> 16));
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-ifetch: Done.\n");
    for (;;) {}   /* park after Done. */
}
