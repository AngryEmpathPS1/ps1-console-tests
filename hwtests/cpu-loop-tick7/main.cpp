/*
 * cpu/cpu-loop-tick7 - does the register decide whether a read between two stores holds them apart? (2026-10-28 night)
 *
 * WHY. hwtests/cpu-loop-tick6 on the SCPH-9001: `sw A; lhu T2; nop; sw A+8` reads 15.15 a turn, on two DRAM pages 12.55, with a
 * nop for the read 7.08 - as if the CPU waited for the Timer 2 read stuck behind the first store; hwtests/sw-ioread (the 1001,
 * JOY_STAT) found the CPU runs on past such a read when nothing reads its value. Register or page?
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration:
 *   <REG>-NEAR   sw (A); lhu v1, <REG>; nop; sw (A + 8); addiu; bnez; nop
 *   <REG>-FAR    the same, the second store 16 KB away
 * REG = T2 (1F801120h), ISTAT (1F801070h), JOY (JOY_STAT, 1F801044h), DPCR (1F8010F0h).
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick7 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));   /* 16 KB + : the FAR store lands a DRAM page away */

extern "C" uint32_t rd16(uint32_t a);
asm(".section .text\n.set push\n.set noreorder\n.global rd16\nrd16:\n  lhu $v0, 0($a0)\n  jr $ra\n  andi $v0, $v0, 0xffff\n.set pop\n");

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(near, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 8(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(far, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 0x4000(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick7\n");
    printf("A read between two stores, by register and page. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; int per; };
    static const V vs[] = {
        { "T2-NEAR", near, T2_COUNT, 1 },
        { "T2-FAR", far, T2_COUNT, 1 },
        { "ISTAT-NEAR", near, 0x1F801070u, 1 },
        { "ISTAT-FAR", far, 0x1F801070u, 1 },
        { "JOY-NEAR", near, 0x1F801044u, 1 },
        { "JOY-FAR", far, 0x1F801044u, 1 },
        { "DPCR-NEAR", near, 0x1F8010F0u, 1 },
        { "DPCR-FAR", far, 0x1F8010F0u, 1 },
    };
    static uint32_t c[8][2];
    for (int i = 0; i < 8; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 8; i++)
        printf("  %-11s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick7: Done.\n");
    for (;;) VSync(0);
}
