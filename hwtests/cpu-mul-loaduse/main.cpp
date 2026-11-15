/*
 * cpu-mul-loaduse: a MULT / DIV that reads a register a RAM load is still filling (the stream refit session, 2026-11-15).
 *
 * WHY. Doom's ceiling builder (R_PlanePrep's span loop, 8002E988h) replayed alone on three consoles runs +2,042 cycles over this
 * emulator in one call, +27..+38 in every one of its 64 iterations (STATUS "Doom's ceiling builder replayed alone"). Each iteration
 * has five `lw rX ; (0-1 instructions) ; mult .., rX ; mflo` where this emulator's MFLO costs 1: it starts the multiply on the
 * load's clock (mulStart: the cycles still owed to the load's shadow taken off), as for a multiply independent of the load - but
 * this one reads the loaded register, so it cannot start before the data arrives. Starting it then, the call reads 38,374.5 here
 * against the consoles' 38,390.5. This times the pattern alone: lw t4 ; n nops ; MULT / DIV reading t4 as rs, as rt, or not ;
 * MFLO, n 0..3 (n 0: the op in the load's delay slot, where it reads the old t4 and should not wait).
 * READ IT. Loops of 64 turns in cached code, cycles by Timer 2, five times each; console - emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP1 0x1F801814u
#define N 64

static uint32_t src[N + 4] __attribute__((aligned(16)));
static volatile uint32_t sink;

#define LOOP(BODY) __asm__ volatile(".set push\n.set noreorder\n" \
    "li $t3, 64\n" \
    "1:\n" BODY \
    "addiu $t3, $t3, -1\n" \
    "bnez $t3, 1b\n" \
    "addiu %[s], %[s], 4\n" \
    ".set pop\n" : [s] "+r"(s), [x] "+r"(x) : [c] "r"(c) : "t3", "t4", "t7", "memory")
#define K0 ""
#define K1 "nop\n"
#define K2 "nop\nnop\n"
#define K3 "nop\nnop\nnop\n"
#define FAM(A, B) switch (n) { case 0: LOOP(A K0 B); break; case 1: LOOP(A K1 B); break; case 2: LOOP(A K2 B); break; default: LOOP(A K3 B); break; }
static uint32_t __attribute__((noinline)) run(int fam, int n) {
    uint32_t* s = src; uint32_t x = 0; const uint32_t c = 3;
    const uint16_t t0 = read16(T2_COUNT);
    switch (fam) {
        case 0: FAM("lw $t4, 0(%[s])\n", "mult $t4, %[c]\nmflo $t7\n"); break;     /* the op reads the loaded register as rs */
        case 1: FAM("lw $t4, 0(%[s])\n", "mult %[c], $t4\nmflo $t7\n"); break;     /* ... as rt */
        case 2: FAM("lw $t4, 0(%[s])\n", "mult %[c], %[c]\nmflo $t7\n"); break;    /* ... not at all */
        case 3: FAM("lw $t4, 0(%[s])\n", "div $t4, %[c]\nmflo $t7\n"); break;      /* a divide reading it */
        case 4: FAM("lw $t4, 0(%[s])\n", "div %[c], %[c]\nmflo $t7\n"); break;     /* a divide not */
        default: FAM("lw $t4, 0(%[s])\n", "addu $t7, $t4, %[c]\n"); break;         /* an ALU reader (the plain load-use) */
    }
    const uint16_t t1 = read16(T2_COUNT);
    sink = x + (uint32_t)(s - src);
    return (uint16_t)(t1 - t0);
}

int main() {
    printf("\ncpu/cpu-mul-loaduse\n");
    printf("Loops of 64 turns in cached code, cycles by Timer 2, five times each: lw t4, n nops, then MULT / DIV reading t4 (rs, rt)\n");
    printf("or not, then MFLO; and an ALU reader. No assertions.\n\n");
    for (int i = 0; i < N + 4; i++) src[i] = 5u + (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {}
    static const char* const NM[6] = { "mult.rs", "mult.rt", "mult.no", "div.rs", "div.no", "addu" };
    static uint16_t r[6][4][5];
    for (int p = 0; p < 5; p++) for (int f = 0; f < 6; f++) for (int n = 0; n < 4; n++) r[f][n][p] = (uint16_t)run(f, n);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int f = 0; f < 6; f++) for (int n = 0; n < 4; n++)
        printf("%-9s n %d   %u %u %u %u %u\n", NM[f], n, (unsigned)r[f][n][0], (unsigned)r[f][n][1], (unsigned)r[f][n][2], (unsigned)r[f][n][3], (unsigned)r[f][n][4]);
    printf("\ncpu/cpu-mul-loaduse: Done.\n");
    for (;;) VSync(0);
}
