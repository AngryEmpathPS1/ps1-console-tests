/*
 * gpu/gpu-a0poll - what a CPU poll loop's reads cost a GPU upload, by the loop's length: 2,048 words by SyncMode 1 blocks of 4, 8
 * and 16 and chopped 4/4 into 1,024x4, polled by loops of a Timer 2 and a CHCR2 read (or the CHCR2 read alone) and 0-16 nops
 * (2026-11-04, for the SCPH-5501).
 *
 * WHY. On the second SCPH-5501 the same 4-word upload reads 7,320-7,350 cycles or 7,560-7,610, never between, run after run, and
 * the programs that read it slow (gpu-a0mode, gpu-a0chop, gpu-a0blk3's chopped transfers) poll it with a loop of 22 instructions
 * where the quick ones (gpu-a0blk3's blocks, gpu-a0phase, gpu-a0after) poll with 19: the compiler hoisted two constants out of one
 * and not the other (hwtests/gpu-a0after read every cell quick). The reads are on-die (Timer 2, CHCR2); this emulator serves them
 * in a block's gap without delaying the next block (DMA_IO_READ_WAITS, fitted on 16-word blocks) and reads both loops quick. This
 * sweeps the loop's period.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks, the display as the runner left it.
 * Each transfer as gpu-a0after's (the GPU idle, GP1(01h), a fill, GP1(04h) = 2, GP0(A0h) at (0,256) 1,024x4, MADR2, BCR2, vblank's
 * first hblank edge + 500 cycles), then in one asm block: a raw Timer 2 read, CHCR2's store and the loop - mode T: lhu Timer 2, lw
 * CHCR2, a jr into a run of nops n from its end, the count, and, bnez on CHCR2's busy bit; mode C: the same with a nop in the Timer
 * 2 read's place. Then 3,000 cycles and zeros until GPUSTAT bit 26. Kinds S4, P4/4, S8, S16; n 0..16; twice each.
 *
 * Mode S spaces the two reads 8 nops apart, as the compiled loops do: there n = 3 is the quick programs' 19-instruction turn and
 * n = 6 the slow ones' 22.
 *
 * READ IT LIKE THIS. P kind mode n rep t last turns: t Timer 2 after the loop less before the store; last (mode T) the loop's last
 * Timer 2 read less the start - the transfer ends between the last two CHCR2 reads; turns the loop's turns. A loop's period is
 * about (t - its exit) / turns. No assertions - compare against your emulator.
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
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

struct R { uint32_t t, last, turns; };

/* mode T: Timer 2 and CHCR2 a turn; n nops (0..31) by a jr into the run. Bounded at 2^16 turns. */
static __attribute__((noinline, noclone)) R pollT(uint32_t chcr, uint32_t n) {
    uint32_t t0, t1, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %4, 2\n"
        "subu  $t3, $t3, $t4\n"
        "lui   $t6, 0x0100\n"
        "move  %3, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %5, 0x10a8($t0)\n"
        "1:\n"
        "lhu   %1, 0x1120($t0)\n"
        "lw    $t2, 0x10a8($t0)\n"
        "jr    $t3\n"
        "addiu %3, %3, 1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "beq   %3, $t5, 8f\n"
        "and   $t2, $t2, $t6\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %2, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1), "=&r"(t9), "=&r"(turns)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.last = (uint16_t)(t1 - t0); r.turns = turns; return r;
}

/* mode S: as T with 8 nops between the two reads, as the compiled loops space them (a jal, the helper's jr and four ALU instructions) */
static __attribute__((noinline, noclone)) R pollS(uint32_t chcr, uint32_t n) {
    uint32_t t0, t1, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %4, 2\n"
        "subu  $t3, $t3, $t4\n"
        "lui   $t6, 0x0100\n"
        "move  %3, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %5, 0x10a8($t0)\n"
        "1:\n"
        "lhu   %1, 0x1120($t0)\n"
        ".rept 8\nnop\n.endr\n"
        "lw    $t2, 0x10a8($t0)\n"
        "jr    $t3\n"
        "addiu %3, %3, 1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "beq   %3, $t5, 8f\n"
        "and   $t2, $t2, $t6\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %2, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1), "=&r"(t9), "=&r"(turns)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.last = (uint16_t)(t1 - t0); r.turns = turns; return r;
}

/* mode C: the CHCR2 read alone (a nop where the Timer 2 read was) */
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

/* kind 'S' SyncMode 1 blocks of a words; 'P' chopped 4/4 */
static void one(char kind, int a, char mode, uint32_t n, int rep) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? (((uint32_t)(WORDS / a) << 16) | (uint32_t)a) : (uint32_t)WORDS);
    const uint32_t chcr = kind == 'S' ? 0x01000201u : (0x11000101u | (2u << 16) | (2u << 20));
    toPlace();
    const R r = mode == 'T' ? pollT(chcr, n) : mode == 'S' ? pollS(chcr, n) : pollC(chcr, n);
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("P %c%d %c %u %d %u %u %u\n", kind, kind == 'P' ? 4 : a, mode, (unsigned)n, rep, (unsigned)r.t, (unsigned)r.last, (unsigned)r.turns);
}

int main() {
    printf("\ngpu/gpu-a0poll\n");
    printf("P kind mode n rep t last turns (2,048 words into 1,024x4 at (0,256) in vblank; S blocks, P4 chopped 4/4; mode S Timer 2, 8 nops, CHCR2; T the two adjacent; C CHCR2 alone; n nops). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one('S', 4, 'T', 0, 0);   /* cache the code */
    static const char K[4] = { 'S', 'P', 'S', 'S' };
    static const int A[4] = { 4, 4, 8, 16 };
    static const char M[3] = { 'S', 'T', 'C' };
    for (int k = 0; k < 4; k++) for (int m = 0; m < 3; m++) for (uint32_t n = 0; n <= 16; n++) for (int r = 0; r < 2; r++) one(K[k], A[k], M[m], n, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll: Done.\n");
    for (;;) VSync(0);
}
