/*
 * mdec/mdec-reset4 - what changes the MDEC's reset window: the state it starts from (2026-10-31, for the SCPH-5501).
 *
 * WHY. mdec-reset2, on the SCPH-5501, under Unirom and under the runner alike: a write 0-5 nops behind the reset is lost,
 * from 6 kept, so at 7 cycles it lands. But mdec-reset's N 0 loses an enable it writes 9 cycles behind the reset, and reads
 * the bus 6 cycles after that, in all 24 runner logs on seven models, while a command at 8 is taken. So the window there
 * is longer, and the two programs differ in what comes before the reset: mdec-reset writes CHCR0 just before it, and its
 * enables are still on, and a table's DMA has just finished. mdec-reset3's enable column went 24 0 24 0 0 0 24: that test
 * is over 4 KB of code, so its cases evict each other and a miss stretches the gap. This one is three small functions.
 *
 * WHAT IT DOES. One run of code: a store of something, 5 nops, the reset, a jump into a slide of nops, and the access, so
 * the gap is 3 + N cycles of cached code for N from 0 to 23. The access is an enable, or a table command, or an enable
 * read back at once. Each from the states below, 8 tries each with the first two thrown away:
 *   0  enables off, a store to RAM before
 *   1  enables on, a store to RAM before
 *   2  enables off, CHCR0 = 0 before
 *   3  enables on, CHCR0 = 0 before
 *   4  as 3, with a 32-word table DMA just finished, which is mdec-reset's own state
 *
 * READ IT LIKE THIS. kept and took are out of 6; `read` is the value read back. Whichever state moves the first N that
 * keeps is the cause. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR0    0x1F801080u
#define BCR0     0x1F801084u
#define CHCR0    0x1F801088u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static volatile uint32_t ramWord;
static uint32_t table[32] __attribute__((aligned(16)));

#define HEAD \
    ".set push\n.set noreorder\n lui $t0, 0x1F80\n lui $t1, 0x8000\n lui $t2, 0x6000\n lui $t3, 0x4000\n ori $t3, $t3, 1\n" \
    "la $t4, 1f\n subu $t4, $t4, %[k]\n" \
    "sw %[v], 0(%[p])\n nop\n nop\n nop\n nop\n nop\n" \
    "sw $t1, 0x1824($t0)\n jr $t4\n nop\n .rept 24\n nop\n .endr\n1:\n"
#define CLOBBER "t0", "t1", "t2", "t3", "t4", "memory"

__attribute__((noinline, aligned(32))) static void goEn(volatile uint32_t* p, uint32_t v, uint32_t n) {
    __asm__ volatile(HEAD "sw $t2, 0x1824($t0)\n .set pop\n" : : [p] "r"(p), [v] "r"(v), [k] "r"(n * 4) : CLOBBER);
}
__attribute__((noinline, aligned(32))) static void goCmd(volatile uint32_t* p, uint32_t v, uint32_t n) {
    __asm__ volatile(HEAD "sw $t3, 0x1820($t0)\n .set pop\n" : : [p] "r"(p), [v] "r"(v), [k] "r"(n * 4) : CLOBBER);
}
__attribute__((noinline, aligned(32))) static uint32_t goRd(volatile uint32_t* p, uint32_t v, uint32_t n) {
    uint32_t r;
    __asm__ volatile(HEAD "sw $t2, 0x1824($t0)\n lw %[r], 0x1824($t0)\n nop\n .set pop\n" : [r] "=&r"(r) : [p] "r"(p), [v] "r"(v), [k] "r"(n * 4) : CLOBBER);
    return r;
}

/* The state each case starts from. */
static void prepare(int state) {
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    waitCycles(400);
    if (state == 1 || state == 3 || state == 4) write32(MDEC1, 0x60000000u);
    if (state == 4) {
        write32(MDEC0, 0x40000001u);
        write32(MADR0, (uint32_t)table & 0x1FFFFFu);
        write32(BCR0, (1u << 16) | 32u);
        write32(CHCR0, 0x01000201u);
        for (uint32_t t0 = now(); (read32(CHCR0) & 0x01000000u) && now() - t0 < 67737u;) {}
    }
    waitCycles(100);
}
static volatile uint32_t* preAddr(int state) { return state >= 2 ? (volatile uint32_t*)CHCR0 : &ramWord; }

static const int TRIES = 8, DROP = 2, STATES = 5, NS = 24;

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset4\n");
    printf("The reset's window from five states. Gap 3 + N cycles of cached code. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);

    static uint8_t kept[STATES][NS], took[STATES][NS];
    static uint32_t rd[STATES][NS];
    for (int s = 0; s < STATES; s++)
        for (int n = 0; n < NS; n++) {
            volatile uint32_t* p = preAddr(s);
            for (int i = 0; i < TRIES; i++) {
                prepare(s); goEn(p, 0, n); waitCycles(400);
                write32(MDEC0, 0x40000001u);
                if (i >= DROP && (read32(MDEC1) & (1u << 28))) kept[s][n]++;
            }
            for (int i = 0; i < TRIES; i++) {
                prepare(s); goCmd(p, 0, n); waitCycles(400);
                if (i >= DROP && (read32(MDEC1) & 0x20000000u)) took[s][n]++;
            }
            for (int i = 0; i < TRIES; i++) { prepare(s); rd[s][n] = goRd(p, 0, n); }
        }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    static const char* NAMES[STATES] = { "enables off, RAM store before", "enables on, RAM store before", "enables off, CHCR0 before",
                                         "enables on, CHCR0 before", "enables on, CHCR0 before, a table DMA just done" };
    for (int s = 0; s < STATES; s++) {
        printf("%d %s\n N  kept took read\n", s, NAMES[s]);
        for (int n = 0; n < NS; n++) printf("%2d  %d  %d  %08x\n", n, (int)kept[s][n], (int)took[s][n], (unsigned)rd[s][n]);
    }
    printf("\nmdec/mdec-reset4: Done.\n");
    for (;;) {}   /* park after Done. */
}
