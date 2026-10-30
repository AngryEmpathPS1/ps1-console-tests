/*
 * gte/gte-sqr-align - does a GTE command read IR1 stale by instruction count, or by where the code sits in its cache line?
 *
 * WHY (2026-10-08). hwtests/gte-irgb's SQR rows on the SCPH-5501, 7501 and 9001, six runs byte for byte alike: after
 * `mtc2 value, IRGB; N nops; SQR` from CACHED code, SQR squares the OLD IR1 at N = 0 and N = 2 and the new at N = 1 and
 * N >= 3; after `mtc2 value, IR1` the old at N = 0 and 1; from uncached code the old at N = 0 only, both. An MFC2 read of
 * IR1 after IRGB is stale for k = N + 1 <= 3 cached (psx-spx: "IR1,IR2 after THREE nop's"), so a command at N = 1 reading
 * the new value and at N = 2 the old one is no single rule - and gte-irgb's probes are wherever the compiler put them
 * (N = 1 with both instructions in one cache line, N = 2 across a line boundary; but N = 3 crosses too and reads new).
 * src/psx.js leaves a command's IR inputs out of irStale (KNOWN-OPEN). This asks whether the line position decides.
 *
 * WHAT IT DOES. Interrupts off. For IRGB (value 7FFFh: IR1 = F80h, MAC1 F04000h) and IR1 (F80h), N = 0..5 nops, and the
 * second mtc2 at word 0, 1, 2 and 3 of its cache line: poison (0), 24 nops, the move, N nops, SQR (sf=0, lm=1), MFC2
 * MAC1. Each probe after the kernel's FlushCache (COLD: every fetch fills) and again at once (WARM: all cached), IR1-3
 * cleared and settled first.
 *
 * READ IT LIKE THIS. Each cell is n (the new value squared, F04000h) or o (the old, 0). A column that differs from its
 * neighbours names the word position; a row whose four cells agree is an instruction count. gte-irgb's own N = 0..3 rows
 * sat at mtc2 words 1, 1, 2, 0 (IRGB) - its "o n o n" is the WARM IRGB cells at (N 0, w1) (N 1, w1) (N 2, w2) (N 3, w0).
 *
 * ON THIS CORE (2026-10-08, v0.377.47, `node hwtests/run.js gte-sqr-align`): every cell n - a command latches its inputs
 * by GTE_LATCH, not by irStale.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

typedef uint32_t (*probe_fn)(uint32_t poison, uint32_t value);

/* One probe: aligned to a line, `pad` nops so the second mtc2 lands at word (25 + pad) & 3 of its line. */
#define PROBE(NAME, REG, PAD, N)                                                  \
    ".balign 16\n.global " #NAME "\n" #NAME ":\n"                                 \
    "  .rept " #PAD "\n  nop\n  .endr\n"                                          \
    "  mtc2 $a0, $" #REG "\n"                                                     \
    "  .rept 24\n  nop\n  .endr\n"                                                \
    "  mtc2 $a1, $" #REG "\n"                                                     \
    "  .rept " #N "\n  nop\n  .endr\n"                                            \
    "  .word 0x4AA00428\n"                                                        \
    "  mfc2 $v0, $25\n"                                                           \
    "  nop\n  jr $ra\n  nop\n"

/* pad p puts the second mtc2 at word (25 + p) & 3: p = 3 -> w0, 0 -> w1, 1 -> w2, 2 -> w3 */
#define ROW(SRC, REG, N) \
    PROBE(SRC##_##N##_w0, REG, 3, N) PROBE(SRC##_##N##_w1, REG, 0, N) PROBE(SRC##_##N##_w2, REG, 1, N) PROBE(SRC##_##N##_w3, REG, 2, N)
#define ROWS(SRC, REG) ROW(SRC, REG, 0) ROW(SRC, REG, 1) ROW(SRC, REG, 2) ROW(SRC, REG, 3) ROW(SRC, REG, 4) ROW(SRC, REG, 5)

__asm__(".set push\n.set noreorder\n.text\n" ROWS(irgb, 28) ROWS(ir1, 9) ".set pop\n");

#define DECL_ROW(SRC, N) extern "C" uint32_t SRC##_##N##_w0(uint32_t, uint32_t), SRC##_##N##_w1(uint32_t, uint32_t), \
    SRC##_##N##_w2(uint32_t, uint32_t), SRC##_##N##_w3(uint32_t, uint32_t);
DECL_ROW(irgb, 0) DECL_ROW(irgb, 1) DECL_ROW(irgb, 2) DECL_ROW(irgb, 3) DECL_ROW(irgb, 4) DECL_ROW(irgb, 5)
DECL_ROW(ir1, 0) DECL_ROW(ir1, 1) DECL_ROW(ir1, 2) DECL_ROW(ir1, 3) DECL_ROW(ir1, 4) DECL_ROW(ir1, 5)
#define TROW(SRC, N) { SRC##_##N##_w0, SRC##_##N##_w1, SRC##_##N##_w2, SRC##_##N##_w3 }
static const probe_fn irgbT[6][4] = { TROW(irgb, 0), TROW(irgb, 1), TROW(irgb, 2), TROW(irgb, 3), TROW(irgb, 4), TROW(irgb, 5) };
static const probe_fn ir1T[6][4] = { TROW(ir1, 0), TROW(ir1, 1), TROW(ir1, 2), TROW(ir1, 3), TROW(ir1, 4), TROW(ir1, 5) };

/* The kernel's FlushCache, A0h function 44h: every I-cache line invalid (bios-window's). */
static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}
static void cop2on() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}
static void clearIR() {
    __asm__ volatile(".set push\n\t.set noreorder\n\t"
                     "mtc2 $zero, $9\n\tmtc2 $zero, $10\n\tmtc2 $zero, $11\n\t"
                     ".rept 24\n\tnop\n\t.endr\n\t.set pop\n\t");
}

static void table(const char *name, const probe_fn t[6][4], uint32_t value) {
    printf("%s -> SQR MAC1 (n = F04000h, o = 0), the move at word 0 1 2 3 of its line\n", name);
    printf("        COLD      WARM\n");
    for (int n = 0; n < 6; n++) {
        char cold[5], warm[5];
        for (int w = 0; w < 4; w++) {
            clearIR(); flushCache();
            uint32_t c = t[n][w](0, value);
            clearIR();
            uint32_t h = t[n][w](0, value);
            cold[w] = c == 0xF04000 ? 'n' : c == 0 ? 'o' : '?';
            warm[w] = h == 0xF04000 ? 'n' : h == 0 ? 'o' : '?';
        }
        cold[4] = warm[4] = 0;
        printf("  N=%d   %s      %s\n", n, cold, warm);
    }
}

int main() {
    printf("\ngte/gte-sqr-align\n");
    printf("SQR after a move to IRGB or IR1, N nops, the move at each word of its cache line, cold and warm. No assertions.\n\n");
    EnterCriticalSection();
    cop2on();
    table("IRGB", irgbT, 0x7FFF);
    printf("\n");
    table("IR1", ir1T, 0x0F80);
    ExitCriticalSection();
    printf("\ngte/gte-sqr-align: Done.\n");
    for (;;) VSync(0);
}
