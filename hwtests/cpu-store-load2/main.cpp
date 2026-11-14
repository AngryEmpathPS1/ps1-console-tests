/*
 * cpu-store-load2: cpu-store-load's copy and send loops taken apart (the stream refit session, 2026-11-14; experiments/mk_storeload2.js).
 *
 * WHY. cpu-store-load on the second SCPH-5501 and the PSone: the copy loop (lw / 3 addiu / bne / sw in the delay slot) reads ~+0.9 a
 * turn on the consoles, the send loop (lw / 2 addiu / sw GP0 / bne / nop) ~-1.1, sw GP0 then lw RAM ~+1.1, its other 26 rows this
 * emulator's. None of its rows has a RAM load followed by a GP0 store, which send has three instructions on (this emulator charges
 * that store 2 cycles). So ld>io (lw RAM, n, sw zero GP0) and ld>ioD (the stored value the loaded one), n 0..3 inside the body; and
 * the loops varied: copyG (the delay-slot store to GP0), copyN (a nop at the target: the load one behind the store), copyB (the
 * store before the branch, a nop in the slot), sendS (send's GP0 store in the delay slot), sendN (a nop between its store and the
 * branch), sendR (its store to RAM). READ IT. As cpu-store-load: console - emulator, over 64 turns.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define N 64

static uint32_t src[N + 4] __attribute__((aligned(16)));
static uint32_t dst[N + 4] __attribute__((aligned(16)));
static volatile uint32_t sink;

#define LOOP(BODY) __asm__ volatile(".set push\n.set noreorder\n" \
    "li $t3, 64\n" \
    "1:\n" BODY \
    "addiu $t3, $t3, -1\n" \
    "addiu %[s], %[s], 4\n" \
    "bnez $t3, 1b\n" \
    "addiu %[d], %[d], 4\n" \
    ".set pop\n" : [s] "+r"(s), [d] "+r"(d), [x] "+r"(x) : [io] "r"(io), [z] "r"(z) : "t3", "t4", "memory")
#define K0 ""
#define K1 "nop\n"
#define K2 "nop\nnop\n"
#define K3 "nop\nnop\nnop\n"
#define FAM(A, B) switch (n) { case 0: LOOP(A K0 B); break; case 1: LOOP(A K1 B); break; case 2: LOOP(A K2 B); break; default: LOOP(A K3 B); break; }
static uint32_t __attribute__((noinline)) run(int fam, int n) {
    uint32_t* s = src; uint32_t* d = dst; uint32_t x = 0; const uint32_t io = 0x1F800000u, z = 0;
    const uint16_t t0 = read16(T2_COUNT);
    switch (fam) {
        case 0: FAM("sw %[z], 0(%[d])\n", "lw $t4, 0(%[s])\n"); break;
        case 1: FAM("sw %[z], 0x1810(%[io])\n", "lw $t4, 0(%[s])\n"); break;
        case 2: FAM("sw %[z], 0(%[d])\n", "lw $t4, 0x1814(%[io])\n"); break;
        case 3: FAM("sw %[z], 0(%[d])\n", "sw %[z], 4(%[d])\n"); break;
        case 4: FAM("sw %[z], 0x1810(%[io])\n", "sw %[z], 0x1810(%[io])\n"); break;
        case 5: FAM("lw $t4, 0(%[s])\n", "sw %[z], 0(%[d])\n"); break;
        case 6: FAM("lw $t4, 0(%[s])\n", "lw $t4, 4(%[s])\n"); break;
        case 7:   /* the game's copy loop as gcc wrote it: the store in the delay slot, the next turn's load behind it */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 0(%[s])\naddiu $t3, $t3, -1\naddiu %[d], %[d], 4\naddiu %[s], %[s], 4\nbne $t3, $t5, 1b\nsw $t4, 0(%[d])\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : : "t3", "t4", "t5", "memory");
            break;
        case 8:  /* its send loop */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 4(%[s])\naddiu $t3, $t3, -1\naddiu %[s], %[s], 4\nsw $t4, 0x1810(%[io])\nbne $t3, $t5, 1b\nnop\n.set pop\n"
                : [s] "+r"(s) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 9: FAM("lw $t4, 0(%[s])\n", "sw %[z], 0x1810(%[io])\n"); break;
        case 10: FAM("lw $t4, 0(%[s])\n", "sw $t4, 0x1810(%[io])\n"); break;
        case 11:   /* copyG */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 0(%[s])\naddiu $t3, $t3, -1\naddiu %[d], %[d], 4\naddiu %[s], %[s], 4\nbne $t3, $t5, 1b\nsw $t4, 0x1810(%[io])\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 12:   /* copyN */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nnop\nlw $t4, 0(%[s])\naddiu $t3, $t3, -1\naddiu %[d], %[d], 4\naddiu %[s], %[s], 4\nbne $t3, $t5, 1b\nsw $t4, 0(%[d])\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 13:   /* copyB */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 0(%[s])\naddiu $t3, $t3, -1\naddiu %[d], %[d], 4\naddiu %[s], %[s], 4\nsw $t4, 0(%[d])\nbne $t3, $t5, 1b\nnop\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 14:   /* sendS */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 4(%[s])\naddiu $t3, $t3, -1\naddiu %[s], %[s], 4\nbne $t3, $t5, 1b\nsw $t4, 0x1810(%[io])\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 15:   /* sendN */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 4(%[s])\naddiu $t3, $t3, -1\naddiu %[s], %[s], 4\nsw $t4, 0x1810(%[io])\nnop\nbne $t3, $t5, 1b\nnop\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        case 16:   /* sendR */
            __asm__ volatile(".set push\n.set noreorder\nli $t3, 63\nli $t5, -1\n1:\nlw $t4, 4(%[s])\naddiu $t3, $t3, -1\naddiu %[s], %[s], 4\nsw $t4, 0(%[d])\nbne $t3, $t5, 1b\nnop\n.set pop\n"
                : [s] "+r"(s), [d] "+r"(d) : [io] "r"(io) : "t3", "t4", "t5", "memory");
            break;
        default: break;
    }
    const uint16_t t1 = read16(T2_COUNT);
    sink = x + (uint32_t)(s - src) + (uint32_t)(d - dst);
    return (uint16_t)(t1 - t0);
}

int main() {
    printf("\ncpu/cpu-store-load2\n");
    printf("Loops of 64 turns in cached code, the GPU idle, cycles by Timer 2, five times each: a pair with n nops between, then four\n");
    printf("instructions off the bus; and the game's copy and send loops. No assertions.\n\n");
    for (int i = 0; i < N; i++) { src[i] = 0; dst[i] = 0; }
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {}
    static const char* const NM[17] = { "ram>ram", "io>ram", "ram>io", "ram>>ram", "io>>io", "ld>ram", "ld>ld", "copy", "send", "ld>io", "ld>ioD", "copyG", "copyN", "copyB", "sendS", "sendN", "sendR" };
    static uint16_t r[17][4][5];
    for (int p = 0; p < 5; p++) for (int f = 0; f < 17; f++) for (int n = 0; n < (f < 7 || f == 9 || f == 10 ? 4 : 1); n++) r[f][n][p] = (uint16_t)run(f, n);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int f = 0; f < 17; f++) for (int n = 0; n < (f < 7 || f == 9 || f == 10 ? 4 : 1); n++)
        printf("%-9s n %d   %u %u %u %u %u\n", NM[f], n, (unsigned)r[f][n][0], (unsigned)r[f][n][1], (unsigned)r[f][n][2], (unsigned)r[f][n][3], (unsigned)r[f][n][4]);
    printf("\ncpu/cpu-store-load2: Done.\n");
    for (;;) VSync(0);
}
