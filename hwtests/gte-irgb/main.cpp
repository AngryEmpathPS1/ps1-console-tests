/*
 * gte/gte-irgb - how many filler opcodes after a write to IRGB (or IR1-3) is
 * the result readable from IR1, IR2, IR3 and ORGB?
 *
 * Two documents, two answers, and no console run (STATUS M233):
 * - psx-spx (nocash): "After writing to IRGB, the result can be read from IR3
 *   after TWO nop's, and from IR1,IR2 after THREE nop's (for uncached code, ONE
 *   nop would work)"; its Store Delay section puts IRGB at 3 cycles because it
 *   "does additionally affect IR1,IR2,IR3".
 * - Sony (GTER3B.TXT lines 55-90): IRGB -> IR1-3, IR1-3 -> ORGB and IRGB -> ORGB
 *   each need TWO nops, and `mtc2 IRGB; mtc2 IR1` back to back is "not
 *   guaranteed".
 * pcsx-redux nugget's gte-latency-lzcs refuted the same "fan-out costs 3"
 * reading for LZCS -> LZCR on four consoles (2 cached, 1 uncached). This is
 * its method applied to IRGB: per pair and per N,
 *
 *     mtc2 poison, W ; 24 nops (settled) ; mtc2 value, W ; N nops ; mfc2 out, R
 *
 * and the smallest N that reads the new value is the answer. N = 0 to 8, each
 * probe once to warm the I-cache (discarded, compared), then timed with
 * interrupts off, cached (KSEG0) and uncached (the same code through KSEG1).
 *
 * THE CONTROL is LZCS -> LZCR, whose answer four consoles already gave: minN 2
 * cached and 1 uncached. A console or emulator that reads anything else there
 * is not running this test the way it claims.
 *
 * INTO A COMMAND: the same writes, N nops, then SQR, whose MAC1 is IR1 squared -
 * psx-spx guesses the restriction "might apply" to a command's IR inputs too.
 *
 * ORDER, the last block, is Sony's "not guaranteed": IRGB written, N nops, IR1
 * written, then IR1 read once everything has settled. The IR1 write reads back
 * if it landed last; F80h (IRGB's expansion) if the fan-out landed after it.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js gte-irgb`): the
 * control reads minN 2 cached and 1 uncached, exactly the four consoles (and
 * this core's lzcrStale, M195) - so the probe sees a store delay when there is
 * one. Every IRGB, IR1-3 and ORGB pair reads minN 0 in both regimes, and both
 * SQR rows F04000h at every N: this core has no store delay there, which
 * NEITHER document supports (both say at least two nops for IR3). ORDER reads
 * 123 everywhere. DuckStation, Redux and the MiSTer core (gte.vhd: IRGB writes
 * IR1-3 on the same clock) all do the same; nothing models it.
 *
 * CONSOLE DAY (2026-10-05/06): the SCPH-5501, 7501 and 9001 byte for byte alike. Cached: IRGB -> IR1 and IR2 minN 3,
 * IR3 2 (UNSTABLE: the cold pass 1), ORGB 0 / 3FFh / 7FFFh at N = 0, 1, 2+ (red and green first), IR1 -> ORGB 1, IR3 ->
 * ORGB 2, IR1 -> IRGB 1, IRGB -> SQR MAC1 old / new / OLD / new at N = 0..3, IR1 -> SQR 2 (UNSTABLE: cold 1). Uncached:
 * every IR row 1, the ORGB rows as cached, both SQR rows 1. ORDER 123 everywhere. psx-spx is right for IR1-3 (three
 * and two nops cached, one uncached); Sony's two nops read IR1/IR2 stale.
 * ON THIS CORE SINCE v0.377.19 (src/psx.js irStale): every row the consoles' but the two SQR rows (minN 0: a command's
 * inputs are GTE_LATCH's, not modelled here) and the UNSTABLE markers (an instruction count has no cold pass).
 */
#include <common.h>
#include <psxapi.h>

#define MAX_N 8
#define STR_(x) #x
#define STR(x) STR_(x)

typedef uint32_t (*probe_fn)(uint32_t, uint32_t);
#define KSEG1(p) ((probe_fn)(((uint32_t)(p)) | 0xA0000000u))

/* One global function per (pair, N), so each can also be called through its
 * KSEG1 alias. .set noreorder keeps the assembler from adding its own nops
 * around the cop2 hazard, which would make N a fiction. */
#define MK(NAME, W, R, N)                                                   \
    extern "C" uint32_t __attribute__((noinline))                           \
    NAME##_##N(uint32_t poison, uint32_t value) {                           \
        uint32_t out;                                                       \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            "mtc2 %1, $" #W "\n\t"                                          \
            ".rept 24\n\tnop\n\t.endr\n\t"                                  \
            "mtc2 %2, $" #W "\n\t"                                          \
            ".rept " #N "\n\tnop\n\t.endr\n\t"                              \
            "mfc2 %0, $" #R "\n\t"                                          \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out) : "r"(poison), "r"(value));                        \
        return out;                                                         \
    }
#define MK_ALL(NAME, W, R) MK(NAME, W, R, 0) MK(NAME, W, R, 1) MK(NAME, W, R, 2) \
    MK(NAME, W, R, 3) MK(NAME, W, R, 4) MK(NAME, W, R, 5) MK(NAME, W, R, 6)       \
    MK(NAME, W, R, 7) MK(NAME, W, R, 8)
#define TABLE(NAME) { NAME##_0, NAME##_1, NAME##_2, NAME##_3, NAME##_4, NAME##_5, NAME##_6, NAME##_7, NAME##_8 }

MK_ALL(lzcs_lzcr, 30, 31)   /* the control */
MK_ALL(irgb_ir1, 28, 9)
MK_ALL(irgb_ir2, 28, 10)
MK_ALL(irgb_ir3, 28, 11)
MK_ALL(irgb_orgb, 28, 29)
MK_ALL(ir1_orgb, 9, 29)
MK_ALL(ir3_orgb, 11, 29)
MK_ALL(ir1_irgb, 9, 28)     /* IRGB reads as ORGB does (psx-spx: "a read-only mirror") */

/* INTO A COMMAND (psx-spx: "When using IR1,IR2,IR3 as parameters for GTE commands,
 * similar timing restrictions might apply"): the write, N nops, SQR (sf=0, lm=1,
 * cop2 0A00428h), MAC1 read - the MFC2 waits for the command. MAC1 = IR1 x IR1:
 * 0 from the poison, F04000h from IR1 = F80h. */
#define MKC(NAME, W, N)                                                     \
    extern "C" uint32_t __attribute__((noinline))                           \
    NAME##_##N(uint32_t poison, uint32_t value) {                           \
        uint32_t out;                                                       \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            "mtc2 %1, $" #W "\n\t"                                          \
            ".rept 24\n\tnop\n\t.endr\n\t"                                  \
            "mtc2 %2, $" #W "\n\t"                                          \
            ".rept " #N "\n\tnop\n\t.endr\n\t"                              \
            ".word 0x4AA00428\n\t"                                          \
            "mfc2 %0, $25\n\t"                                              \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out) : "r"(poison), "r"(value));                        \
        return out;                                                         \
    }
#define MKC_ALL(NAME, W) MKC(NAME, W, 0) MKC(NAME, W, 1) MKC(NAME, W, 2) MKC(NAME, W, 3) \
    MKC(NAME, W, 4) MKC(NAME, W, 5) MKC(NAME, W, 6) MKC(NAME, W, 7) MKC(NAME, W, 8)
MKC_ALL(irgb_sqr, 28)
MKC_ALL(ir1_sqr, 9)

/* ORDER: IRGB = value, N nops, IR1 = poison, settle, read IR1. */
#define MKO(N)                                                              \
    extern "C" uint32_t __attribute__((noinline))                           \
    order_##N(uint32_t ir1v, uint32_t irgbv) {                              \
        uint32_t out;                                                       \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            "mtc2 $zero, $28\n\t"                                           \
            ".rept 24\n\tnop\n\t.endr\n\t"                                  \
            "mtc2 %2, $28\n\t"                                              \
            ".rept " #N "\n\tnop\n\t.endr\n\t"                              \
            "mtc2 %1, $9\n\t"                                               \
            ".rept 24\n\tnop\n\t.endr\n\t"                                  \
            "mfc2 %0, $9\n\t"                                               \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out) : "r"(ir1v), "r"(irgbv));                          \
        return out;                                                         \
    }
MKO(0) MKO(1) MKO(2) MKO(3) MKO(4) MKO(5) MKO(6) MKO(7) MKO(8)

struct Pair {
    const char* name;
    probe_fn fn[MAX_N + 1];
    uint32_t poison, value, mask, expect;
    int wreg;   /* 9 or 11: IR2 and IR3 (or IR1 and IR2) held at 0 first */
};

static const Pair PAIRS[] = {
    { "LZCS->LZCR (control)", TABLE(lzcs_lzcr), 0x00000001, 0x00010000, 0xFFFFFFFF, 15, 0 },
    { "IRGB->IR1", TABLE(irgb_ir1), 0x0000, 0x7FFF, 0xFFFFFFFF, 0x0F80, 0 },
    { "IRGB->IR2", TABLE(irgb_ir2), 0x0000, 0x7FFF, 0xFFFFFFFF, 0x0F80, 0 },
    { "IRGB->IR3", TABLE(irgb_ir3), 0x0000, 0x7FFF, 0xFFFFFFFF, 0x0F80, 0 },
    { "IRGB->ORGB", TABLE(irgb_orgb), 0x0000, 0x7FFF, 0xFFFFFFFF, 0x7FFF, 0 },
    { "IR1->ORGB", TABLE(ir1_orgb), 0x0000, 0x0F80, 0xFFFFFFFF, 0x001F, 9 },
    { "IR3->ORGB", TABLE(ir3_orgb), 0x0000, 0x0F80, 0xFFFFFFFF, 0x7C00, 11 },
    { "IR1->IRGB", TABLE(ir1_irgb), 0x0000, 0x0F80, 0xFFFFFFFF, 0x001F, 9 },
    { "IRGB->SQR MAC1", TABLE(irgb_sqr), 0x0000, 0x7FFF, 0xFFFFFFFF, 0xF04000, 0 },
    { "IR1->SQR MAC1", TABLE(ir1_sqr), 0x0000, 0x0F80, 0xFFFFFFFF, 0xF04000, 9 },
};
static const probe_fn ORDER[MAX_N + 1] = TABLE(order);

static void gteEnable() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

/* IR1-3 to 0 except the one under test, settled. */
static void clearIR() {
    __asm__ volatile(".set push\n\t.set noreorder\n\t"
                     "mtc2 $zero, $9\n\tmtc2 $zero, $10\n\tmtc2 $zero, $11\n\t"
                     ".rept 24\n\tnop\n\t.endr\n\t.set pop\n\t");
}

static void sweep(const Pair& p, int uncached) {
    uint32_t warm[MAX_N + 1], got[MAX_N + 1];
    for (int n = 0; n <= MAX_N; n++) {
        probe_fn f = uncached ? KSEG1(p.fn[n]) : p.fn[n];
        clearIR();
        warm[n] = f(p.poison, p.value) & p.mask;
    }
    for (int n = 0; n <= MAX_N; n++) {
        probe_fn f = uncached ? KSEG1(p.fn[n]) : p.fn[n];
        clearIR();
        got[n] = f(p.poison, p.value) & p.mask;
    }
    int minN = -1, unstable = 0;
    for (int n = 0; n <= MAX_N; n++) {
        if (minN < 0 && got[n] == p.expect) minN = n;
        if (got[n] != warm[n]) unstable = 1;
    }
    printf("%c %-22s minN=%2d  got=[", uncached ? 'U' : 'C', p.name, minN);
    for (int n = 0; n <= MAX_N; n++) printf("%s%x", n ? " " : "", got[n]);
    printf("]%s\n", unstable ? " UNSTABLE" : "");
    if (unstable) {
        printf("  warm pass=[");
        for (int n = 0; n <= MAX_N; n++) printf("%s%x", n ? " " : "", warm[n]);
        printf("]\n");
    }
}

static void order(int uncached) {
    uint32_t got[MAX_N + 1];
    for (int n = 0; n <= MAX_N; n++) (uncached ? KSEG1(ORDER[n]) : ORDER[n])(0x123, 0x7FFF);   /* warm */
    for (int n = 0; n <= MAX_N; n++) got[n] = (uncached ? KSEG1(ORDER[n]) : ORDER[n])(0x123, 0x7FFF);
    printf("%c ORDER IRGB;N;IR1 -> IR1=[", uncached ? 'U' : 'C');
    for (int n = 0; n <= MAX_N; n++) printf("%s%x", n ? " " : "", got[n]);
    printf("]  (123 = the IR1 write landed last, f80 = IRGB's fan-out did)\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngte/gte-irgb\n");
    printf("IRGB/IR1-3 -> IR1-3/ORGB store delay, in filler opcodes (psx-spx: IR3 2, IR1/IR2 3; Sony: 2)\n");
    printf("minN = the first N that reads the new value; got = the reads at N = 0..%d.\n", MAX_N);
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    gteEnable();
    for (int u = 0; u < 2; u++) {
        for (unsigned i = 0; i < sizeof(PAIRS) / sizeof(PAIRS[0]); i++) sweep(PAIRS[i], u);
        order(u);
        printf("\n");
    }
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) VSync(0);
}
