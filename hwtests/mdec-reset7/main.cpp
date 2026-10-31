/*
 * mdec/mdec-reset7 - does a reset take longer from a busy MDEC? (2026-10-31, for the SCPH-5501)
 *
 * WHY. mdec-reset2 and 4: from an idle MDEC a write 7 cycles or more behind the reset lands, whatever came before. mdec-reset's
 * N 0 loses a write at 9, but mdec-reset6, the same code with N 1 run first, loses nothing, so the loss needs N 0 to be
 * the program's first call. The reading: the first call runs from a cold cache and loses its enable, which leaves the MDEC
 * waiting for a table that never comes, and the second call, the row that is printed, resets it from THAT. mdec-reset4's
 * five states were all idle. These are not.
 *
 * WHAT IT DOES. As mdec-reset4: a store of CHCR0, 5 nops, the reset, a jump into a slide of nops, the access, so the gap is
 * 3 + N cycles of cached code, here N 0 to 63. From:
 *   0  idle, enables on (mdec-reset4's state 1, to check the tool)
 *   1  a table command with no data, enables on: mdec-reset's stuck state, as near as a test can make it
 *   2  the same with its enables off
 *   3  the same as 1, 5 of its 32 words written by hand
 *   4  a decode command for 32 words with 6 written: one macroblock in and the decoder wanting more
 *   5  a decode command for 6 words, all written: the picture made and nobody reading it
 * READ IT LIKE THIS. Each state prints three rows of 64 digits, N left to right, of 6: enables kept, commands taken, and for
 * reads . for the status, B for the bus. Where kept turns on is when the reset is over. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define CHCR0    0x1F801088u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

#define HEAD \
    ".set push\n.set noreorder\n lui $t0, 0x1F80\n lui $t1, 0x8000\n lui $t2, 0x6000\n lui $t3, 0x4000\n ori $t3, $t3, 1\n" \
    "la $t4, 1f\n subu $t4, $t4, %[k]\n" \
    "sw $zero, 0x1088($t0)\n nop\n nop\n nop\n nop\n nop\n" \
    "sw $t1, 0x1824($t0)\n jr $t4\n nop\n .rept 64\n nop\n .endr\n1:\n"
#define CLOBBER "t0", "t1", "t2", "t3", "t4", "memory"

__attribute__((noinline, aligned(32))) static void goEn(uint32_t n) {
    __asm__ volatile(HEAD "sw $t2, 0x1824($t0)\n .set pop\n" : : [k] "r"(n * 4) : CLOBBER);
}
__attribute__((noinline, aligned(32))) static void goCmd(uint32_t n) {
    __asm__ volatile(HEAD "sw $t3, 0x1820($t0)\n .set pop\n" : : [k] "r"(n * 4) : CLOBBER);
}
__attribute__((noinline, aligned(32))) static uint32_t goRd(uint32_t n) {
    uint32_t r;
    __asm__ volatile(HEAD "sw $t2, 0x1824($t0)\n lw %[r], 0x1824($t0)\n nop\n .set pop\n" : [r] "=&r"(r) : [k] "r"(n * 4) : CLOBBER);
    return r;
}

/* Six words of a macroblock: six blocks, each a DC and an end of block. */
static void block(int words) { for (int i = 0; i < words; i++) write32(MDEC0, 0xFE000400u); }

static void prepare(int state) {
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    waitCycles(600);
    switch (state) {
        case 0: write32(MDEC1, 0x60000000u); break;
        case 1: write32(MDEC1, 0x60000000u); write32(MDEC0, 0x40000001u); break;
        case 2: write32(MDEC0, 0x40000001u); break;
        case 3: write32(MDEC1, 0x60000000u); write32(MDEC0, 0x40000001u); block(5); break;
        case 4: write32(MDEC0, 0x38000020u); block(6); break;
        case 5: write32(MDEC0, 0x38000006u); block(6); break;
    }
    waitCycles(300);
}

static const int TRIES = 8, DROP = 2, STATES = 6, NS = 64;
static uint8_t kept[STATES][NS], took[STATES][NS];
static uint32_t rd[STATES][NS];
static uint32_t statusBefore[STATES];

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset7\n");
    printf("The reset's window from a busy MDEC. Gap 3 + N cycles of cached code, N 0 to 63. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int s = 0; s < STATES; s++) {
        prepare(s);
        statusBefore[s] = read32(MDEC1);
        for (int n = 0; n < NS; n++) {
            for (int i = 0; i < TRIES; i++) {
                prepare(s); goEn(n); waitCycles(800);
                write32(MDEC0, 0x40000001u);
                if (i >= DROP && (read32(MDEC1) & (1u << 28))) kept[s][n]++;
            }
            for (int i = 0; i < TRIES; i++) {
                prepare(s); goCmd(n); waitCycles(800);
                if (i >= DROP && (read32(MDEC1) & 0x20000000u)) took[s][n]++;
            }
            for (int i = 0; i < TRIES; i++) { prepare(s); rd[s][n] = goRd(n); }
        }
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    static const char* NAMES[STATES] = { "idle, enables on", "table command, no data, enables on", "table command, no data, enables off",
                                         "table command, 5 words, enables on", "decode, 6 words of 32", "decode, 6 of 6, nobody reading" };
    for (int s = 0; s < STATES; s++) {
        printf("%d %s, status %08x\n kept ", s, NAMES[s], (unsigned)statusBefore[s]);
        for (int n = 0; n < NS; n++) printf("%d", (int)kept[s][n]);
        printf("\n took ");
        for (int n = 0; n < NS; n++) printf("%d", (int)took[s][n]);
        printf("\n read ");
        for (int n = 0; n < NS; n++) printf("%c", rd[s][n] == 0x60000000u ? 'B' : (rd[s][n] & 0xFFFF0000u) == 0x80040000u ? '.' : '?');
        printf("\n");
    }
    printf("\nmdec/mdec-reset7: Done.\n");
    for (;;) {}   /* park after Done. */
}
