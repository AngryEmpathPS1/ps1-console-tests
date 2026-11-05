/*
 * gpu/gpu-dma-turn - dma-arb's turn in pieces: what each access between runs of IOR units costs during a GPU upload, against the
 * blocks (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-iorun on the second SCPH-5501: dma-arb's IOR unit (lw DPCR; addiu; bnez; nop) during a 16-word-block upload
 * makes one read a block (1.02-1.04) on the console AND on this emulator, the blocks at their quiet pace. So dma-arb's IOR / TMR rows
 * (~430 cycles later on every console, 11 turns to this emulator's 10) are the rest of its turn: two RAM loads of globals (lw a0,
 * g_per; lw v0, g_fn - reloaded after every call), the jalr to the units, read32(Timer 2) and read32(CHCR) as calls. The console's
 * turn leaves ~35 cycles for those, this emulator's ~112. This times each piece.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; an 8,192-word upload by SyncMode 1 blocks of 16 (512 blocks) or 4 into
 * GP0(A0h) at (0,256). The CHCR2 store, then K turns of: [pieces] and U units (lw DPCR; addiu; bnez; nop), then BCR2 (the blocks
 * left) and Timer 2. Variants V (bit set = piece in):
 *   1  two RAM loads of a global pair (lw t6, 0(g); lw t9, 4(g); nop) at the turn's start
 *   2  two on-die reads after the units, inline (lw Timer 2; lw CHCR2)
 *   4  those two reads as calls (bal to `lw v0, 0(a0); jr ra; nop`, ra kept in a register) - with 2 set
 *   8  one RAM store after the units (sw to the global pair)
 * V in 0, 1, 2, 3, 6, 7, 8; U 8 (K 30 at 16 words, 60 at 4); each at four refresh places. A line: T bs V try t left - t the turns'
 * cycles, left the blocks BCR2 held.
 *
 * READ IT LIKE THIS. Blocks moved = total - left; cycles a turn = t / K; a piece's cost = its variant's turn less V 0's. No assertions -
 * compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-turn --frames=3000 --quiet=1500`.
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
#define WORDS 8192
#define UNITS 8

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static volatile uint32_t glob[4] = { 1, 2, 3, 4 };
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* K turns of variant V; returns the cycles (16 bits), *left the BCR2 high half */
#define TURNV(V) \
static __attribute__((noinline, noclone)) uint32_t turn##V(uint32_t k, uint32_t chcr, uint32_t *left) { \
    uint32_t t0, t1, b; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t7, %5\n" \
        "move  $t8, %3\n" \
        "move  $t1, $ra\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %4, 0x10a8($t0)\n" \
        "2:\n" \
        ".if (" #V " & 1)\n lw $t6, 0($t7)\n lw $t9, 4($t7)\n nop\n.endif\n" \
        "li    $t4, " "8" "\n" \
        "1:\n" \
        "lw    $t5, 0x10f0($t0)\n" \
        "addiu $t4, $t4, -1\n" \
        "bnez  $t4, 1b\n" \
        "nop\n" \
        ".if (" #V " & 2)\n" \
        ".if (" #V " & 4)\n addiu $a0, $t0, 0x1120\n bal 7f\n nop\n addiu $a0, $t0, 0x10a8\n bal 7f\n nop\n" \
        ".else\n lw $t6, 0x1120($t0)\n lw $t9, 0x10a8($t0)\n nop\n.endif\n" \
        ".endif\n" \
        ".if (" #V " & 8)\n sw $t8, 8($t7)\n.endif\n" \
        "addiu $t8, $t8, -1\n" \
        "bnez  $t8, 2b\n" \
        "nop\n" \
        "lw    %2, 0x10a4($t0)\n" \
        "lhu   %1, 0x1120($t0)\n" \
        "b     9f\n" \
        "nop\n" \
        "7:\n" \
        "lw    $v0, 0($a0)\n" \
        "jr    $ra\n" \
        "nop\n" \
        "9:\n" \
        "move  $ra, $t1\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(b) \
        : "r"(k), "r"(chcr), "r"(glob) \
        : "t0", "t1", "t4", "t5", "t6", "t7", "t8", "t9", "a0", "v0", "ra", "memory"); \
    *left = b >> 16; return (uint16_t)(t1 - t0); \
}
TURNV(0) TURNV(1) TURNV(2) TURNV(3) TURNV(6) TURNV(7) TURNV(8)
typedef uint32_t (*Turn)(uint32_t, uint32_t, uint32_t *);
static const Turn TURN[7] = { turn0, turn1, turn2, turn3, turn6, turn7, turn8 };
static const int VV[7] = { 0, 1, 2, 3, 6, 7, 8 };

static void one(uint32_t bs, int vi, int tr, uint32_t p, int print) {
    const uint32_t blocks = WORDS / bs, k = bs == 16 ? 30u : 60u;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / 256) << 16) | 256u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (blocks << 16) | bs);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    uint32_t left = 0;
    const uint32_t t = TURN[vi](k, 0x01000201u, &left);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) { __asm__ volatile(".rept 40\nnop\n.endr\n"); }
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (print) printf("T %u %d %d %u %u\n", (unsigned)bs, VV[vi], tr, (unsigned)t, (unsigned)left);
}

int main() {
    printf("\ngpu/gpu-dma-turn\n");
    printf("T bs V try t left (K turns of [pieces V] and %d IOR units during a SyncMode 1 upload of 8,192 words; t their cycles, left the blocks BCR2 held after). No assertions.\n\n", UNITS);
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int v = 0; v < 7; v++) one(16, v, 0, 0, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 };
    for (int b = 0; b < 2; b++) for (int v = 0; v < 7; v++) for (int tr = 0; tr < 4; tr++) one(BS[b], v, tr, (uint32_t)tr * 64u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-turn: Done.\n");
    for (;;) VSync(0);
}
