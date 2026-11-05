/*
 * gpu/gpu-a0pollbs - does a lone CHCR2 read a turn drag a GPU upload at EVERY block size when its turn runs one cycle over a whole
 * number of blocks? gpu-a0poll's mode C at blocks of 1, 2, 4, 8 and 16 words, n 0..28 (2026-11-05, for the SCPH-5501).
 *
 * WHY. On the second SCPH-5501 a loop of one CHCR2 read a turn drags a SyncMode 1 upload by ~0.85 cycles a block when its turn
 * is one cycle longer than a block's period (bs + 10 on the bus and idle): gpu-a0poll's mode C at 4-word n = 5 (a 15-cycle turn,
 * 7,726 against a quick 7,300) and 8-word n = 9 (19 cycles, 4,888 / 4,690), and hwtests/gpu-burstline's compiled poll (11
 * instructions, its value used 3 on: 15 cycles, 3,880 against ~3,670). This emulator gives the drag when a lone on-die read takes
 * the bus from a block due at its own cycle (experiments/_mkiolone.js, PSX_CB_IOL 1: gpu-burstline 203 -> 5.8 cycles RMS) - but
 * then hwtests/gpu-a0rate2's 1-word cells (a ~55-cycle compiled poll, five 11-cycle blocks a turn) flip a whole poll step late at a
 * dozen places where the console and this emulator without the tie agree place for place. Is that because 1-word blocks never
 * give the bus to a read at their due cycle on the console, or because that loop's turn there is not a whole number of blocks?
 *
 * WHAT IT DOES. gpu-a0poll's mode C exactly (its pollC asm: nop, lw CHCR2, a jr into n of 32 nops, the count, and, bnez, nop;
 * Timer 2 raw before the store and after the loop; bounded at 2^16 turns) on 2,048 words into 1,024x4 at (0,256) in vblank, set
 * up as gpu-a0poll's (a fill, GP1(04h) = 2, GP0(A0h), vblank's first hblank edge + 500 cycles), SyncMode 1 blocks of 1, 2, 4, 8
 * and 16 words, n 0..28, twice each. The turn is 10 + n cycles quiet (gpu-a0poll's mode C on the console from n = 10), so a turn
 * one over k blocks falls at: 1-word n = 2, 13, 24; 2-word n = 3, 15, 27; 4-word n = 5, 19; 8-word n = 9, 27; 16-word n = 17.
 *
 * READ IT LIKE THIS. P kind mode n rep t last turns, as gpu-a0poll: t Timer 2 after the loop less before the store, turns the
 * loop's turns. A drag is t over the kind's quick rows at those n. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0pollbs --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    for (int i = 0; i < 200000 && read16(T1_COUNT) == e; i++) {}
    waitT2(500);
}

struct R { uint32_t t, last, turns; };

/* mode C: the CHCR2 read alone (gpu-a0poll's pollC, instruction for instruction) */
static __attribute__((noinline, noclone)) R pollC(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "lui   $t6, 0x0100\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw    $t2, 0x10a8($t0)\n"
        "jr    $t3\n"
        "addiu %2, %2, 1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "beq   %2, $t5, 8f\n"
        "and   $t2, $t2, $t6\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9), "=&r"(turns)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.last = 0; r.turns = turns; return r;
}

/* SyncMode 1 blocks of a words */
static void one(int a, uint32_t n, int rep, int print) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / a) << 16) | (uint32_t)a);
    toPlace();
    const R r = pollC(0x01000201u, n);
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    if (print) printf("P S%d C %u %d %u %u %u\n", a, (unsigned)n, rep, (unsigned)r.t, (unsigned)r.last, (unsigned)r.turns);
}

int main() {
    printf("\ngpu/gpu-a0pollbs\n");
    printf("P kind mode n rep t last turns (2,048 words into 1,024x4 at (0,256) in vblank; S blocks of 1-16 words; mode C CHCR2 alone; n nops). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one(4, 0, 0, 0);   /* cache the code */
    static const int A[5] = { 1, 2, 4, 8, 16 };
    for (int k = 0; k < 5; k++) for (uint32_t n = 0; n <= 28; n++) for (int r = 0; r < 2; r++) one(A[k], n, r, 1);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0pollbs: Done.\n");
    for (;;) VSync(0);
}
