/*
 * cpu/cpu-extras - the CPU timings that rest on a table, a datasheet or
 * another emulator: multiply and divide by operand, lwc2 back to back and
 * among ALU work, swc2 and mfc2 behind a GTE command, the write buffer's
 * depth, a store to the CD drive's registers, and PRID.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, CPU rows H, F, L, AU and Q, GPU row 51):
 *   MULTIPLY AND DIVIDE (M208, MUL_FAST). 7 cycles from a MULT to the first
 *     MFLO that does not wait when rs is small, 10 when medium, 14 when large,
 *     37 for a divide. nocash's DIAG measured the small case on his console
 *     (six nops free, the seventh a cycle). The other three are psx-spx's table
 *     plus the same one cycle, and the ranges are psx-spx's and psxact's:
 *     |rs| below 800h, below 100000h. psx-spx's signed table reads "Med
 *     FFF00000h..FFFFF801h" and "Slow 80000000h..FFF00001h", which a
 *     twelve-bit and a twenty-one-bit Booth range (this core's) make
 *     FFFFF7FFh and FFEFFFFFh. A new multiply waiting for the one in flight,
 *     and MTHI/MTLO not waiting, are DuckStation's and psxact's, unmeasured.
 *     So is what MFLO reads after a MTLO that lands while a multiply runs:
 *     here the MTLO's value, because the multiply wrote LO when it issued.
 *   LWC2 (M158, M207, LWC2_EXTRA). 8 cycles back to back on a retail
 *     pipeline. Sony's bus capture reads 7 on its dev board, which runs a
 *     cycle off retail on these accesses (M207). An lwc2 arms no load shadow
 *     here, so ALU work after it does not overlap it; a lw's does.
 *   SWC2 AND MFC2 BEHIND A GTE COMMAND. Both wait for it to end, and a cycle
 *     more (DuckStation's StallUntilGTEComplete; the extra cycle extrapolated),
 *     as CFC2 does. RTPS is 15 cycles (psx-spx). gte-hold asks what such a wait
 *     does to a write before it; this asks how long it is.
 *   THE WRITE BUFFER (M207). Four words deep (the LSI CW33000 datasheet, and
 *     pcsx-redux PR #2091's silicon test), 6 bus cycles for a burst's first
 *     word and 2 for each after it on the same page.
 *   A STORE TO THE CD DRIVE. Charged here as a read of that region. Nothing
 *     has timed one; store-cost times the SPU and Expansion 2.
 *   PRID. 00000002h here (psx-spx), never read on a console for this project.
 *   (SIO_BAUD's reset value, 00DCh here, is also on the audit's list. It
 *   cannot be read from a program Unirom uploaded over SIO1: Unirom has set
 *   the baud rate by then.)
 *
 * WHAT IT DOES. Every row is a loop of 256 iterations timed on Timer 2 at the
 * system clock, run twice (the second, with a warm I-cache, is reported),
 * minus a baseline loop that holds nops where the row's instructions are.
 * Interrupts are off. Rows print cycles per iteration in hundredths.
 *   MUL/DIV AT K = 0: MULT, MULTU, DIV or DIVU and then MFLO at once, over
 *     operands on both sides of every range boundary; "latency" is the extra
 *     plus one (the multiply's own cycle).
 *   SWEEPS: K nops between them, K = 0..16 for a small, a medium and a large
 *     rs, and K = 0..40 for DIVU. The first K with no extra is latency - 1.
 *   IN FLIGHT: a large MULTU, then a small one (at once, and 5 nops later),
 *     then MFLO; a DIVU then a small MULTU; a large MULTU then MTLO (or MTHI)
 *     then MFLO (MFHI): the extra, and the value read.
 *   LWC2: eight back to back from RAM and from the scratchpad, and lw the same
 *     way; then one followed by K ALU ops (K = 0..6), and lw the same way.
 *   SWC2 / MFC2: RTPS, K nops (0..20), then swc2 or mfc2 of SXY2; and whether
 *     the word read is RTPS's result or what SXY2 held before it.
 *   WRITE BUFFER: N stores to RAM back to back, then 40 nops (N = 1..8).
 *   CD STORE: sb to 1F801800h (the index register: it selects index 0), one
 *     and four back to back, and lbu from it.
 *   PRID.
 *
 * ANSWERED by the SCPH-5501, 7501 and 9001 (console day; v0.377.26-.28, src/psx.js MUL_LAST, LWC2_EXTRA, CD_WRITE_SHORT):
 * the latencies as M208 has them; an MFLO one cycle before the result does not wait (every sweep steps 2 -> 0); a new
 * multiply replaces the running one, behind a divide too; MTLO / MTHI end the wait (0.00, reading their value); an
 * lwc2 costs a load's 7 from RAM and nothing from the scratchpad (LWC2_EXTRA 0), with no shadow; a CD store holds the
 * bus 6 cycles (sb x1 1.92, x4 4.23). AT v0.377.28 every one of those rows reads the consoles' to a few hundredths.
 * Still different: the WRITE BUFFER row, 0 0 0 0 0 2.25 2.19 4.14 there and 0 0 0 0 2.08 3.22 4.31 5.88 here
 * (hwtests/wb-drain maps it).
 * ON THIS CORE AT v0.267 (`node hwtests/run.js cpu-extras`, under a minute):
 *   PRID 00000002.
 *   MUL/DIV: latency 7 / 10 / 14 by rs alone (rt never matters), signed
 *     FFFFF800h..000007FFh fast and FFF00000h..000FFFFFh medium: FFFFF801h is
 *     fast here and FFF00001h medium, psx-spx's "801h" and "001h" read as the
 *     slips they look like. Every divide 37, whatever its operands. The
 *     sweeps' knees: 6, 9 and 13 nops free, and 36 for DIVU.
 *   IN FLIGHT: a second multiply waits for the first (19 = 13 + 6, or 14 with
 *     5 nops between) and MFLO reads the second's 15; behind a divide, 42.
 *     MTLO (MTHI) during a multiply: MFLO (MFHI) still waits for the multiply
 *     (12) and reads the MTLO's 5A5A5A5Ah, because the multiply wrote LO when
 *     it issued. A console whose multiplier writes LO when it finishes reads
 *     5B05B058h there.
 *   LWC2: back to back from RAM 8 cycles each (extra 7.09), from the
 *     scratchpad 2; lw 6.09 and 1. With K ALU ops after each, an lwc2 stays
 *     7.02-7.10 extra at every K (nothing overlaps it), a lw falls from 5.10
 *     to 4.06 (its load shadow).
 *   SWC2 / MFC2: extra 15 at K 0, a cycle less for each nop, 0 from K 14, the
 *     two the same; the word read is RTPS's result at every K.
 *   WRITE BUFFER: no extra for N = 1..4, then 2.09, 3.21, 4.32, 5.89.
 *   CD STORE: sb 7 extra (8 cycles), lbu 8 (9).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u

typedef uint32_t (*fn2)(uint32_t, uint32_t);

/* A loop of 256 iterations between two reads of Timer 2's count ($t9 holds 1F800000h). The nop after the last
 * read keeps the compiler's next instruction out of its load delay slot. */
#define PRE  ".set push\n\t.set noreorder\n\t.set noat\n\t" \
             "lui $t9, 0x1f80\n\tli $t8, 256\n\tlhu %[t0], 0x1120($t9)\n\tnop\n1:\n\t"
#define POST "addiu $t8, $t8, -1\n\tbnez $t8, 1b\n\tnop\n\tlhu %[t1], 0x1120($t9)\n\tnop\n\t.set pop\n\t"
#define OUTS [t0] "=&r"(t0), [t1] "=&r"(t1)
#define CLOBBERS "t7", "t8", "t9", "hi", "lo", "memory"

/* Multiply/divide then MFLO, K nops between. */
#define MULFN(NAME, OPC)                                                            \
    template <int K> __attribute__((noinline)) uint32_t NAME(uint32_t a, uint32_t b) { \
        uint32_t t0, t1;                                                            \
        __asm__ volatile(PRE OPC " %[a], %[b]\n\t.rept %[k]\n\tnop\n\t.endr\n\tmflo $t7\n\t" POST \
            : OUTS : [a] "r"(a), [b] "r"(b), [k] "i"(K) : CLOBBERS);                \
        return (uint16_t)(t1 - t0);                                                 \
    }
MULFN(tMultu, "multu")
MULFN(tMult, "mult")
MULFN(tDivu, "divu $zero,")
MULFN(tDiv, "div $zero,")
MULFN(tBase, "nop #")        /* the baseline: a nop where the multiply was ('#' comments out the operands) */

#define T8(F, o) F<o + 0>, F<o + 1>, F<o + 2>, F<o + 3>, F<o + 4>, F<o + 5>, F<o + 6>, F<o + 7>
static const fn2 MULTU_K[17] = { T8(tMultu, 0), T8(tMultu, 8), tMultu<16> };
static const fn2 DIVU_K[41] = { T8(tDivu, 0), T8(tDivu, 8), T8(tDivu, 16), T8(tDivu, 24), T8(tDivu, 32), tDivu<40> };
static const fn2 BASE_K[41] = { T8(tBase, 0), T8(tBase, 8), T8(tBase, 16), T8(tBase, 24), T8(tBase, 32), tBase<40> };

static uint32_t twice(fn2 f, uint32_t a, uint32_t b) { f(a, b); return f(a, b); }
/* Extra cycles per iteration, in hundredths. */
static int32_t extra(uint32_t t, uint32_t base) { return ((int32_t)t - (int32_t)base) * 100 / 256; }
static void hund(int32_t v) {
    if (v < 0) { printf("-"); v = -v; }
    printf("%d.%02d", v / 100, v % 100);
}

/* IN FLIGHT: fixed sequences, each returning its time and the last value it read. */
static uint32_t inflightValue;
#define INFL(NAME, BODY)                                                            \
    __attribute__((noinline)) uint32_t NAME(uint32_t a, uint32_t b) {               \
        uint32_t t0, t1, v;                                                         \
        __asm__ volatile(PRE BODY POST "move %[v], $t7\n\t"                          \
            : OUTS, [v] "=&r"(v) : [a] "r"(a), [b] "r"(b), [x] "r"(0x5A5A5A5Au), [c] "r"(5u) : CLOBBERS, "t6"); \
        inflightValue = v;                                                          \
        return (uint16_t)(t1 - t0);                                                 \
    }
INFL(fMulMul0, "multu %[a], %[c]\n\tmultu %[b], %[c]\n\tmflo $t7\n\t")
INFL(bMulMul0, "nop\n\tnop\n\tmflo $t7\n\t")
INFL(fMulMul5, "multu %[a], %[c]\n\t.rept 5\n\tnop\n\t.endr\n\tmultu %[b], %[c]\n\tmflo $t7\n\t")
INFL(bMulMul5, "nop\n\t.rept 5\n\tnop\n\t.endr\n\tnop\n\tmflo $t7\n\t")
INFL(fDivMul, "divu $zero, %[a], %[c]\n\tmultu %[b], %[c]\n\tmflo $t7\n\t")
INFL(fMulMtlo, "multu %[a], %[c]\n\tmtlo %[x]\n\tmflo $t7\n\t")
INFL(bMulMtlo, "nop\n\tmtlo %[x]\n\tmflo $t7\n\t")
INFL(fMulMthi, "multu %[a], %[c]\n\tmthi %[x]\n\tmfhi $t7\n\t")
INFL(bMulMthi, "nop\n\tmthi %[x]\n\tmfhi $t7\n\t")

/* LWC2 and lw: eight back to back from `p`, and one followed by K ALU ops, four times. */
#define LOADFN(NAME, BODY)                                                          \
    __attribute__((noinline)) uint32_t NAME(uint32_t p, uint32_t) {                 \
        uint32_t t0, t1;                                                            \
        __asm__ volatile(PRE BODY POST : OUTS : [p] "r"(p) : CLOBBERS, "t2", "t3", "t4", "t5", "t6"); \
        return (uint16_t)(t1 - t0);                                                 \
    }
LOADFN(fLwc2x8, "lwc2 $0, 0(%[p])\n\tlwc2 $1, 4(%[p])\n\tlwc2 $2, 8(%[p])\n\tlwc2 $3, 12(%[p])\n\t"
                "lwc2 $0, 16(%[p])\n\tlwc2 $1, 20(%[p])\n\tlwc2 $2, 24(%[p])\n\tlwc2 $3, 28(%[p])\n\t")
LOADFN(fLwx8, "lw $t3, 0(%[p])\n\tlw $t4, 4(%[p])\n\tlw $t5, 8(%[p])\n\tlw $t6, 12(%[p])\n\t"
              "lw $t3, 16(%[p])\n\tlw $t4, 20(%[p])\n\tlw $t5, 24(%[p])\n\tlw $t6, 28(%[p])\n\t")
LOADFN(bX8, ".rept 8\n\tnop\n\t.endr\n\t")
/* Four loads to four different registers, each followed by K ALU ops that do not read them. */
#define ALU1(K) "\n\t.rept " #K "\n\taddu $t5, $t5, $t6\n\t.endr\n\t"
#define ALUK(A, B, C, D, K) A ALU1(K) B ALU1(K) C ALU1(K) D ALU1(K)
#define LOADK(K)                                                                    \
    LOADFN(fLwc2k##K, ALUK("lwc2 $0, 0(%[p])", "lwc2 $1, 4(%[p])", "lwc2 $2, 8(%[p])", "lwc2 $3, 12(%[p])", K)) \
    LOADFN(fLwk##K, ALUK("lw $t3, 0(%[p])", "lw $t4, 4(%[p])", "lw $t7, 8(%[p])", "lw $t2, 12(%[p])", K)) \
    LOADFN(bLk##K, ALUK("nop", "nop", "nop", "nop", K))
LOADK(0) LOADK(1) LOADK(2) LOADK(3) LOADK(4) LOADK(5) LOADK(6)
static const fn2 LWC2K[7] = { fLwc2k0, fLwc2k1, fLwc2k2, fLwc2k3, fLwc2k4, fLwc2k5, fLwc2k6 };
static const fn2 LWK[7] = { fLwk0, fLwk1, fLwk2, fLwk3, fLwk4, fLwk5, fLwk6 };
static const fn2 BLK[7] = { bLk0, bLk1, bLk2, bLk3, bLk4, bLk5, bLk6 };

/* SWC2 / MFC2 of SXY2 behind RTPS (4A180001h, as games encode it), K nops between. SXY2 is set to a marker first,
 * so the word read tells RTPS's result from what was there before. */
#define MARK 0x12341234u
#define GTEFN(NAME, CMD, READ)                                                      \
    template <int K> __attribute__((noinline)) uint32_t NAME(uint32_t p, uint32_t) { \
        uint32_t t0, t1;                                                            \
        __asm__ volatile(PRE "mtc2 %[m], $14\n\tnop\n\tnop\n\t" CMD "\n\t.rept %[k]\n\tnop\n\t.endr\n\t" READ "\n\tnop\n\t" POST \
            : OUTS : [p] "r"(p), [m] "r"(MARK), [k] "i"(K) : CLOBBERS);             \
        return (uint16_t)(t1 - t0);                                                 \
    }
GTEFN(tSwc2, ".word 0x4A180001", "swc2 $14, 0(%[p])")
GTEFN(tSwc2b, "nop", "swc2 $14, 0(%[p])")
GTEFN(tMfc2, ".word 0x4A180001", "mfc2 $t7, $14\n\tnop\n\tsw $t7, 0(%[p])")
GTEFN(tMfc2b, "nop", "mfc2 $t7, $14\n\tnop\n\tsw $t7, 0(%[p])")
#define T7(F, o) F<o + 0>, F<o + 1>, F<o + 2>, F<o + 3>, F<o + 4>, F<o + 5>, F<o + 6>
static const fn2 SWC2K[21] = { T7(tSwc2, 0), T7(tSwc2, 7), T7(tSwc2, 14) };
static const fn2 SWC2B[21] = { T7(tSwc2b, 0), T7(tSwc2b, 7), T7(tSwc2b, 14) };
static const fn2 MFC2K[21] = { T7(tMfc2, 0), T7(tMfc2, 7), T7(tMfc2, 14) };
static const fn2 MFC2B[21] = { T7(tMfc2b, 0), T7(tMfc2b, 7), T7(tMfc2b, 14) };

/* WRITE BUFFER: N stores to consecutive words, then 40 nops. */
template <int N> __attribute__((noinline)) uint32_t tStores(uint32_t p, uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".set wbo, 0\n\t.rept %[n]\n\tsw $zero, wbo(%[p])\n\t.set wbo, wbo + 4\n\t.endr\n\t"
                     ".rept 40\n\tnop\n\t.endr\n\t" POST
        : OUTS : [p] "r"(p), [n] "i"(N) : CLOBBERS);
    return (uint16_t)(t1 - t0);
}
template <int N> __attribute__((noinline)) uint32_t tStoresB(uint32_t p, uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".rept %[n]\n\tnop\n\t.endr\n\t.rept 40\n\tnop\n\t.endr\n\t" POST
        : OUTS : [p] "r"(p), [n] "i"(N) : CLOBBERS);
    return (uint16_t)(t1 - t0);
}
static const fn2 STORES[9] = { tStores<0>, tStores<1>, tStores<2>, tStores<3>, tStores<4>, tStores<5>, tStores<6>, tStores<7>, tStores<8> };
static const fn2 STORESB[9] = { tStoresB<0>, tStoresB<1>, tStoresB<2>, tStoresB<3>, tStoresB<4>, tStoresB<5>, tStoresB<6>, tStoresB<7>, tStoresB<8> };

/* CD STORE: the CD's index register. */
LOADFN(fCdSb1, "sb $zero, 0(%[p])\n\t")
LOADFN(fCdSb4, "sb $zero, 0(%[p])\n\tsb $zero, 0(%[p])\n\tsb $zero, 0(%[p])\n\tsb $zero, 0(%[p])\n\t")
LOADFN(fCdLb1, "lbu $t3, 0(%[p])\n\t")
LOADFN(fCdLb4, "lbu $t3, 0(%[p])\n\tlbu $t4, 0(%[p])\n\tlbu $t5, 0(%[p])\n\tlbu $t6, 0(%[p])\n\t")
LOADFN(bN1, "nop\n\t")
LOADFN(bN4, "nop\n\tnop\n\tnop\n\tnop\n\t")

#define CTC2(R, V) __asm__ volatile("ctc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))
#define MTC2(R, V) __asm__ volatile("mtc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))
static void gteEnable() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}
static void scene() {   /* gte-hold's: RT identity, TR 0, the screen offset, H 100h, V0 (256, 128, 0) */
    CTC2(0, 0x1000); CTC2(1, 0); CTC2(2, 0x1000); CTC2(3, 0); CTC2(4, 0x1000);
    CTC2(5, 0); CTC2(6, 0); CTC2(7, 0x1000);
    CTC2(24, 160 << 16); CTC2(25, 120 << 16); CTC2(26, 0x100);
    CTC2(27, 0x100); CTC2(28, 0);
    MTC2(0, (0x80u << 16) | 0x100u); MTC2(1, 0x200);
}

uint32_t ramBuf[64] __attribute__((aligned(64)));

struct Op { const char* name; fn2 f; uint32_t a, b; };

int main() {
    printf("\ncpu/cpu-extras\n");
    printf("Loops of 256, the second run of each, minus a loop with nops in place: extra cycles an iteration.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    gteEnable();

    uint32_t prid;
    __asm__ volatile("mfc0 %0, $15\n\tnop" : "=r"(prid));
    printf("PRID (cop0 r15): %08x\n\n", prid);

    printf("MUL/DIV then MFLO at once: extra, and latency = extra + 1\n");
    const uint32_t b0 = twice(tBase<0>, 0, 0);
    static const Op ops[] = {
        { "multu 0        x 3       ", tMultu<0>, 0x00000000u, 3 },
        { "multu 7FF      x 3       ", tMultu<0>, 0x000007FFu, 3 },
        { "multu 800      x 3       ", tMultu<0>, 0x00000800u, 3 },
        { "multu FFFFF    x 3       ", tMultu<0>, 0x000FFFFFu, 3 },
        { "multu 100000   x 3       ", tMultu<0>, 0x00100000u, 3 },
        { "multu 7FFFFFFF x 3       ", tMultu<0>, 0x7FFFFFFFu, 3 },
        { "multu 80000000 x 3       ", tMultu<0>, 0x80000000u, 3 },
        { "multu FFFFFFFF x 3       ", tMultu<0>, 0xFFFFFFFFu, 3 },
        { "multu 3        x FFFFFFFF", tMultu<0>, 3, 0xFFFFFFFFu },
        { "multu FFFFFFFF x 0       ", tMultu<0>, 0xFFFFFFFFu, 0 },
        { "mult  FFFFFFFF x 3       ", tMult<0>, 0xFFFFFFFFu, 3 },
        { "mult  FFFFF801 x 3       ", tMult<0>, 0xFFFFF801u, 3 },
        { "mult  FFFFF800 x 3       ", tMult<0>, 0xFFFFF800u, 3 },
        { "mult  FFFFF7FF x 3       ", tMult<0>, 0xFFFFF7FFu, 3 },
        { "mult  FFF00001 x 3       ", tMult<0>, 0xFFF00001u, 3 },
        { "mult  FFF00000 x 3       ", tMult<0>, 0xFFF00000u, 3 },
        { "mult  FFEFFFFF x 3       ", tMult<0>, 0xFFEFFFFFu, 3 },
        { "mult  80000000 x 3       ", tMult<0>, 0x80000000u, 3 },
        { "mult  7FFFF    x 3       ", tMult<0>, 0x0007FFFFu, 3 },
        { "mult  3        x 80000000", tMult<0>, 3, 0x80000000u },
        { "divu  100      / 7       ", tDivu<0>, 100, 7 },
        { "divu  FFFFFFFF / 1       ", tDivu<0>, 0xFFFFFFFFu, 1 },
        { "divu  1        / FFFFFFFF", tDivu<0>, 1, 0xFFFFFFFFu },
        { "divu  5        / 0       ", tDivu<0>, 5, 0 },
        { "divu  0        / 0       ", tDivu<0>, 0, 0 },
        { "div   100      / 7       ", tDiv<0>, 100, 7 },
        { "div   -100     / 7       ", tDiv<0>, (uint32_t)-100, 7 },
        { "div   80000000 / -1      ", tDiv<0>, 0x80000000u, 0xFFFFFFFFu },
        { "div   5        / 0       ", tDiv<0>, 5, 0 },
        { "div   -5       / 0       ", tDiv<0>, (uint32_t)-5, 0 },
    };
    for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        const int32_t e = extra(twice(ops[i].f, ops[i].a, ops[i].b), b0);
        printf("  %s  extra ", ops[i].name); hund(e); printf("  latency "); hund(e + 100); printf("\n");
    }

    printf("\nSWEEPS: K nops between the multiply and MFLO, extra at each K\n");
    static const uint32_t rsK[3] = { 3, 0x1234, 0x12345678 };
    for (int r = 0; r < 3; r++) {
        printf("  multu %8x x 3, K = 0..16:", rsK[r]);
        for (int k = 0; k <= 16; k++) { printf(" "); hund(extra(twice(MULTU_K[k], rsK[r], 3), twice(BASE_K[k], 0, 0))); }
        printf("\n");
    }
    printf("  divu 100 / 7, K = 0..40:");
    for (int k = 0; k <= 40; k++) { printf(" "); hund(extra(twice(DIVU_K[k], 100, 7), twice(BASE_K[k], 0, 0))); }
    printf("\n");

    printf("\nIN FLIGHT (a = 12345678h slow, b = 3 fast, both times 5; x = 5A5A5A5Ah):\n");
    struct Pair { const char* name; fn2 f, base; };
    static const Pair pairs[] = {
        { "multu a; multu b; mflo          ", fMulMul0, bMulMul0 },
        { "multu a; 5 nops; multu b; mflo  ", fMulMul5, bMulMul5 },
        { "divu a/5; multu b; mflo         ", fDivMul, bMulMul0 },
        { "multu a; mtlo x; mflo           ", fMulMtlo, bMulMtlo },
        { "multu a; mthi x; mfhi           ", fMulMthi, bMulMthi },
    };
    for (unsigned i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        const uint32_t base = twice(pairs[i].base, 0x12345678u, 3);
        const int32_t e = extra(twice(pairs[i].f, 0x12345678u, 3), base);
        printf("  %s extra ", pairs[i].name); hund(e); printf("  read %08x\n", inflightValue);
    }
    printf("  (a x 5 = 00000001:5B05B058, b x 5 = 15; a / 5 = 03A4A4B1 rem 3)\n");

    printf("\nLWC2 / LW, eight back to back (extra per load):\n");
    const uint32_t ram = (uint32_t)ramBuf, spad = 0x1F800000u;
    const uint32_t bx8 = twice(bX8, ram, 0);
    printf("  lwc2 RAM "); hund(extra(twice(fLwc2x8, ram, 0), bx8) / 8); printf("   lwc2 scratchpad "); hund(extra(twice(fLwc2x8, spad, 0), bx8) / 8);
    printf("   lw RAM "); hund(extra(twice(fLwx8, ram, 0), bx8) / 8); printf("   lw scratchpad "); hund(extra(twice(fLwx8, spad, 0), bx8) / 8); printf("\n");
    printf("  one load then K ALU ops, four times (extra per load), K = 0..6:\n");
    printf("    lwc2 RAM:"); for (int k = 0; k <= 6; k++) { printf(" "); hund(extra(twice(LWC2K[k], ram, 0), twice(BLK[k], ram, 0)) / 4); } printf("\n");
    printf("    lw   RAM:"); for (int k = 0; k <= 6; k++) { printf(" "); hund(extra(twice(LWK[k], ram, 0), twice(BLK[k], ram, 0)) / 4); } printf("\n");

    printf("\nSWC2 / MFC2 of SXY2, K nops after RTPS (extra), K = 0..20:\n");
    scene();
    ramBuf[0] = 0;
    SWC2K[20](ram, 0);
    const uint32_t result = ramBuf[0];
    printf("  swc2:"); for (int k = 0; k <= 20; k++) { printf(" "); hund(extra(twice(SWC2K[k], ram, 0), twice(SWC2B[k], ram, 0))); } printf("\n");
    printf("  mfc2:"); for (int k = 0; k <= 20; k++) { printf(" "); hund(extra(twice(MFC2K[k], ram, 0), twice(MFC2B[k], ram, 0))); } printf("\n");
    printf("  word read (N = RTPS's result %08x, O = the %08x before it):\n", result, MARK);
    printf("  swc2:"); for (int k = 0; k <= 20; k++) { ramBuf[0] = 0; SWC2K[k](ram, 0); printf(" %c", ramBuf[0] == result ? 'N' : ramBuf[0] == MARK ? 'O' : '?'); } printf("\n");
    printf("  mfc2:"); for (int k = 0; k <= 20; k++) { ramBuf[0] = 0; MFC2K[k](ram, 0); printf(" %c", ramBuf[0] == result ? 'N' : ramBuf[0] == MARK ? 'O' : '?'); } printf("\n");

    printf("\nWRITE BUFFER: N stores back to back then 40 nops (extra per iteration), N = 1..8:\n ");
    for (int n = 1; n <= 8; n++) { printf(" "); hund(extra(twice(STORES[n], ram, 0), twice(STORESB[n], ram, 0))); }
    printf("\n");

    printf("\nCD STORE (1F801800h, extra per access):\n");
    const uint32_t cd = 0x1F801800u, n1 = twice(bN1, cd, 0), n4 = twice(bN4, cd, 0);
    printf("  sb x1 "); hund(extra(twice(fCdSb1, cd, 0), n1)); printf("   sb x4 "); hund(extra(twice(fCdSb4, cd, 0), n4) / 4);
    printf("   lbu x1 "); hund(extra(twice(fCdLb1, cd, 0), n1)); printf("   lbu x4 "); hund(extra(twice(fCdLb4, cd, 0), n4) / 4); printf("\n");

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/cpu-extras: Done.\n");
    for (;;) VSync(0);
}
