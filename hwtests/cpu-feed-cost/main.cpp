/*
 * cpu-feed-cost: WHAT THE PIECES OF A GAME'S GPU FEED LOOP COST THE CPU - RAM copies, GPUSTAT reads, GP0 stores, each alone.
 *
 * WHY. hwtests/gpu-doom-feed (Doom's packets through the game's feed loop) timed every call. By the words a call sent
 * (experiments/audit/doomfeedcost.js; the second SCPH-5501 twice and the PSone, alike to 0.2 cycles): a call that sends
 * NOTHING - eight words copied RAM to RAM, one GPUSTAT read, the timer read around it - takes 201.5 cycles on the consoles
 * and 194.0 on v0.377.348; a call that sends eight words 333.5 against 334.4. So the call's base is 7.5 cycles dear on the
 * console and a word sent to GP0 about 1 cycle cheap, two errors of opposite sign inside one loop. Which instruction?
 *
 * WHAT. Seven loops of 64 turns in cached code, the GPU idle, each timed by Timer 2 on the system clock (a read before
 * and after), five times each (the first fetches the code):
 *   empty      the loop alone
 *   ram copy   d[i] = s[i]                        (a load from RAM, a store to RAM)
 *   ram store  d[i] = a register                  (a store to RAM)
 *   ram load   x += s[i]                          (a load from RAM)
 *   stat read  x += GPUSTAT                       (a load from the GPU's port)
 *   gp0 reg    GP0 = 0 from a register            (a store to the GPU's port: GP0(00h), no operation)
 *   gp0 ram    GP0 = s[i], s all zero             (a load from RAM, then the store to the port - the feed's send loop)
 *   t2 read    x += Timer 2's counter             (a load from the timers' port)
 * The same instructions in both machines, so only the difference between a console's row and the emulator's means
 * anything: over 64 turns it is that instruction's cost in 1/64ths of a cycle. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js cpu-feed-cost --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define N 64

static uint32_t src[N] __attribute__((aligned(16)));
static uint32_t dst[N] __attribute__((aligned(16)));
static volatile uint32_t sink;

#define LOOP(BODY) __asm__ volatile(".set push\n.set noreorder\n" \
    "li $t3, 64\n" \
    "1:\n" BODY \
    "addiu $t3, $t3, -1\n" \
    "addiu %[s], %[s], 4\n" \
    "bnez $t3, 1b\n" \
    "addiu %[d], %[d], 4\n" \
    ".set pop\n" : [s] "+r"(s), [d] "+r"(d), [x] "+r"(x) : [io] "r"(io), [z] "r"(z) : "t3", "t4", "memory")
static uint32_t __attribute__((noinline)) run(int which) {
    uint32_t* s = src; uint32_t* d = dst; uint32_t x = 0; const uint32_t io = 0x1F800000u, z = 0;
    const uint16_t t0 = read16(T2_COUNT);
    switch (which) {
        case 0: LOOP(""); break;
        case 1: LOOP("lw $t4, 0(%[s])\nnop\nsw $t4, 0(%[d])\n"); break;
        case 2: LOOP("sw %[z], 0(%[d])\n"); break;
        case 3: LOOP("lw $t4, 0(%[s])\nnop\naddu %[x], %[x], $t4\n"); break;
        case 4: LOOP("lw $t4, 0x1814(%[io])\nnop\naddu %[x], %[x], $t4\n"); break;
        case 5: LOOP("sw %[z], 0x1810(%[io])\n"); break;
        case 6: LOOP("lw $t4, 0(%[s])\nnop\nsw $t4, 0x1810(%[io])\n"); break;
        default: LOOP("lhu $t4, 0x1120(%[io])\nnop\naddu %[x], %[x], $t4\n"); break;
    }
    const uint16_t t1 = read16(T2_COUNT);
    sink = x + (uint32_t)(s - src) + (uint32_t)(d - dst);
    return (uint16_t)(t1 - t0);
}

int main() {
    printf("\ncpu/cpu-feed-cost\n");
    printf("Loops of 64 turns in cached code, the GPU idle, cycles by Timer 2 (a read before and after), five times each. No assertions.\n\n");
    for (int i = 0; i < N; i++) { src[i] = 0; dst[i] = 0; }
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {}
    static const char* const NM[8] = { "empty", "ram copy", "ram store", "ram load", "stat read", "gp0 reg", "gp0 ram", "t2 read" };
    static uint32_t r[8][5];
    for (int p = 0; p < 5; p++) for (int w = 0; w < 8; w++) r[w][p] = run(w);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int w = 0; w < 8; w++) printf("%-10s %u %u %u %u %u\n", NM[w], (unsigned)r[w][0], (unsigned)r[w][1], (unsigned)r[w][2], (unsigned)r[w][3], (unsigned)r[w][4]);
    printf("\ncpu/cpu-feed-cost: Done.\n");
    for (;;) VSync(0);
}
