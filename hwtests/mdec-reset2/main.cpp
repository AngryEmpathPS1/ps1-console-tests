/*
 * mdec/mdec-reset2 - the MDEC's reset cycle by cycle: what a read, an enable and a command do at each distance behind it
 * (2026-10-31, for the SCPH-5501).
 *
 * WHY. hwtests/mdec-reset row N 0 lost its enables in every log since the runner, 24 of 24 on seven models, and read
 * 60000000h, the value it had just written. The rows around it fix no mechanism: on this core's clock that write lands 9
 * cycles behind the reset and is lost, a read at 15 is wrong, but a write at 14 and a write at 15 are kept and a command at 8
 * is taken. Either the reset clears the enables some cycles after its write and stays busy a little longer, or something
 * else. And 60000000h is either the status during a reset or the bus still holding the write. psxsplash's movies lost
 * this write once their code was in the cache, so a real program can fall in it (STATUS "MDEC1's enable is lost").
 *
 * WHAT IT DOES. Each case is a run of cached instructions: a store of the reset, N nops, one more access, nothing to
 * disturb it (interrupts off, no DMA). Three runs, the last one kept, so the cache is warm and N is N cycles.
 *   read    reset, N nops, read the status. 80000000h would be the bus: the reset's own value. 60000000h is a status.
 *   wr+rd   reset, N nops, enables, read at once. mdec-reset's own row, closer in.
 *   en      reset, N nops, enables; 400 cycles on, a table command; the status shows bit 28 if the enable was kept.
 *   cmd     reset, N nops, a table command; 400 cycles on, the status shows it busy with 1Fh to come if it was taken.
 *   cm+rd   reset, N nops, a table command, read at once.
 * And once each, reset and enables in one write (E0000000h), and in two with a different value (40000000h, data-in only),
 * read at once, which tells the bus from a status again.
 *
 * READ IT LIKE THIS. The N at which `read` turns to 80040000h is when the status is back; the N at which `en` turns from
 * lost to kept is when the reset stops clearing what is written. No assertions, the numbers are the result.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

#define SLED ".rept %[n]\n nop\n .endr\n"
#define TOP ".set push\n.set noreorder\n lui $t0, 0x1F80\n lui $t1, 0x8000\n lui $t2, 0x6000\n lui $t3, 0x4000\n ori $t3, $t3, 1\n"
#define END ".set pop\n"
#define CLOBBER "t0", "t1", "t2", "t3", "memory"

template <int N> __attribute__((noinline)) static uint32_t rd() {
    uint32_t v;
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v) : [n] "i"(N) : CLOBBER);
    return v;
}
template <int N> __attribute__((noinline)) static uint32_t wrRd() {
    uint32_t v;
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "sw $t2, 0x1824($t0)\n lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v) : [n] "i"(N) : CLOBBER);
    return v;
}
template <int N> __attribute__((noinline)) static uint32_t en() {
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "sw $t2, 0x1824($t0)\n" END : : [n] "i"(N) : CLOBBER);
    return 0;
}
template <int N> __attribute__((noinline)) static uint32_t cmd() {
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "sw $t3, 0x1820($t0)\n" END : : [n] "i"(N) : CLOBBER);
    return 0;
}
template <int N> __attribute__((noinline)) static uint32_t cmRd() {
    uint32_t v;
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "sw $t3, 0x1820($t0)\n lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v) : [n] "i"(N) : CLOBBER);
    return v;
}

typedef uint32_t (*Case)();
#define ALL(f) { f<0>, f<1>, f<2>, f<3>, f<4>, f<5>, f<6>, f<7>, f<8>, f<9>, f<10>, f<11>, f<12>, f<13>, f<14>, f<15>, \
                 f<16>, f<17>, f<18>, f<19>, f<20>, f<21>, f<22>, f<23>, f<24>, f<25>, f<26>, f<27>, f<28>, f<29>, f<30>, f<31> }
static const Case RD[32] = ALL(rd), WRRD[32] = ALL(wrRd), EN[32] = ALL(en), CMD[32] = ALL(cmd), CMRD[32] = ALL(cmRd);

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
/* Three runs of one case, each from a settled MDEC, so the last is from the cache. */
static uint32_t warm(Case c) {
    uint32_t v = 0;
    for (int i = 0; i < 3; i++) { settle(); v = c(); if (i < 2) waitCycles(400); }
    return v;
}

static uint32_t oneWrite(uint32_t first, uint32_t second) {
    uint32_t v = 0;
    for (int i = 0; i < 3; i++) {
        settle();
        __asm__ volatile(".set push\n.set noreorder\n lui $t0, 0x1F80\n sw %[a], 0x1824($t0)\n sw %[b], 0x1824($t0)\n lw %[v], 0x1824($t0)\n nop\n .set pop\n"
                         : [v] "=&r"(v) : [a] "r"(first), [b] "r"(second) : "t0", "memory");
    }
    return v;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset2\n");
    printf("The MDEC's reset by the cycle. N nops of cached code behind it, then one access. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    static uint32_t rows[32][5];
    for (int n = 0; n < 32; n++) {
        rows[n][0] = warm(RD[n]);
        rows[n][1] = warm(WRRD[n]);
        warm(EN[n]); waitCycles(400); write32(MDEC0, 0x40000001u); rows[n][2] = read32(MDEC1);
        warm(CMD[n]); waitCycles(400); rows[n][3] = read32(MDEC1);
        rows[n][4] = warm(CMRD[n]);
    }
    const uint32_t both = oneWrite(0x80000000u, 0x80000000u), inOnly = oneWrite(0x80000000u, 0x40000000u), outOnly = oneWrite(0x80000000u, 0x20000000u), none = oneWrite(0x80000000u, 0u);
    settle();
    write32(MDEC1, 0xE0000000u); waitCycles(400); write32(MDEC0, 0x40000001u);
    const uint32_t together = read32(MDEC1);
    settle();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf(" N  read      wr+rd     en        cmd       cm+rd\n");
    for (int n = 0; n < 32; n++)
        printf("%2d  %08x  %08x  %08x  %08x  %08x\n", n, (unsigned)rows[n][0], (unsigned)rows[n][1], (unsigned)rows[n][2], (unsigned)rows[n][3], (unsigned)rows[n][4]);
    printf("\nTwo stores and a read, second store's value: 80000000 %08x, 40000000 %08x, 20000000 %08x, 00000000 %08x\n", (unsigned)both, (unsigned)inOnly, (unsigned)outOnly, (unsigned)none);
    printf("E0000000 in one write, 400 cycles, a table command: %08x\n", (unsigned)together);
    printf("\nmdec/mdec-reset2: Done.\n");
    for (;;) {}   /* park after Done. */
}
