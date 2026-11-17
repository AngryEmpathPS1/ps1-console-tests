/*
 * bus/exp1-read2 - what bus/exp1-read left to ask (2026-11-16, the busValue session; main's ask on AngryEmpath's go). The
 * SCPH-1001 with AngryEmpath's GameShark (our cart boot ROM in its SST 29EE020) ran exp1-read three times, identical: at the
 * kernel's settings EXP1 costs what this core has, but under other EXP1_DELAY / COM_DELAY settings the recovery (bit 8) and
 * floating (bit 10) periods add nothing to a read ALONE and a lot to one right behind another, the hold bit (9) nothing at all,
 * and the pre-strobe bit (11) COM3 - 3 a bus cycle. So:
 *   P  HOW AN IDLE GAP TAKES THE RECOVERY / FLOATING PERIOD UP: two byte reads at 1F000000h with 0..12 no-ops between, under each
 *      setting (the no-ops' own cycles taken off): where the cost stops falling is the period's reach.
 *   X  ACROSS REGIONS: an EXP1 read, g no-ops, then a read of the BIOS ROM, of EXP2, of RAM - and the other way round - with EXP1's
 *      recovery on: whose wait is it?
 *   E  EXP2 (1F802000h, no device; its register 1F80101Ch, the kernel's 00070777h has bits 8, 9 and 10): the same sweep exp1-read
 *      made of EXP1's - exp1-read's EXP2 rows read 15 a byte back to back where the rule from EXP1 gives 14.
 *   I  WHAT THE CART'S OTHER ADDRESSES RETURN: bytes at 1F020000h+ (psx-spx's Datel I/O: STB at +10h, the switch at +18h) and
 *      1F060000h+ (the comms link's data in), every fourth address of the first 64 and a few further on; 1F060000h read twice.
 * HARD RULE, as exp1-read: NOT ONE STORE INTO 1F000000h-1F07FFFFh - gen() and gen2() emit loads only; the stores here go to RAM,
 * Timer 2's mode, EXP1_DELAY (1F801008h), EXP2_DELAY (1F80101Ch) and COM_DELAY (1F801020h), each put back before a row prints
 * and checked at the end. Bits 16-31 of the two Delay/Size registers are never changed.
 * TIMING as exp1-read: a generated routine between two Timer 2 reads, the cache flushed, one cold call thrown away, the smallest
 * of six warm calls less the K = 0 routine ("+n": the largest was n more). Interrupts off.
 * RUN IT HERE FIRST (PREDICTED.txt): PSX_VC_CART=cartrom/build/cart.bin PSX_MODEL=SCPH-1001, the runner's locked start.
 */
#include "../stamp-template/stamp.h"

#define EXP1_DELAY 0x1F801008u
#define EXP2_DELAY 0x1F80101Cu
#define COM_DELAY  0x1F801020u

typedef uint32_t (*TimedFn)(uint32_t a, uint32_t b);
static uint32_t code[160] __attribute__((aligned(64)));
enum { LB = 0x20, LH = 0x21, LW = 0x23, LBU = 0x24, LHU = 0x25 };

static void STAMP_NOINLINE FlushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}
static void isLoad(int op) { if (op != LB && op != LH && op != LW && op != LBU && op != LHU) { printf("not a load (%x): refused\n", op); for (;;) {} } }
static int head() { code[0] = 0x3C191F80u; code[1] = 0x97221120u; return 2; }                       /* lui t9,1F80h; lhu v0,1120h(t9) */
static void tail(int n) { code[n++] = 0x97231120u; code[n++] = 0x03E00008u; code[n++] = 0x00621023u; FlushCache(); }   /* lhu v1; jr ra; subu v0,v1,v0 */
/* K loads of one width from a0, `gap` no-ops between them */
static void STAMP_NOINLINE gen(int op, int k, int gap) {
    isLoad(op);
    int n = head();
    for (int i = 0; i < k; i++) { code[n++] = ((uint32_t)op << 26) | (4u << 21) | (8u << 16); for (int g = 0; g < gap && i + 1 < k; g++) code[n++] = 0; }
    tail(n);
}
/* one load from a0, `gap` no-ops, one load from a1 */
static void STAMP_NOINLINE gen2(int opA, int gap, int opB) {
    isLoad(opA); isLoad(opB);
    int n = head();
    code[n++] = ((uint32_t)opA << 26) | (4u << 21) | (8u << 16);
    for (int g = 0; g < gap; g++) code[n++] = 0;
    code[n++] = ((uint32_t)opB << 26) | (5u << 21) | (9u << 16);                                     /* load t1, 0(a1) */
    tail(n);
}
static uint32_t lo, hi;
static void STAMP_NOINLINE timeit(uint32_t a, uint32_t b) {
    TimedFn f = (TimedFn)(uint32_t)code;
    lo = 0xFFFF; hi = 0;
    (void)f(a, b);
    for (int r = 0; r < 6; r++) { scatter(rnd(48)); const uint32_t d = f(a, b) & 0xFFFF; if (d < lo) lo = d; if (d > hi) hi = d; }
}
static uint32_t base0;
static uint32_t exp1Kernel, exp2Kernel, comKernel;
static void restore() { write32(EXP1_DELAY, exp1Kernel); write32(EXP2_DELAY, exp2Kernel); write32(COM_DELAY, comKernel); }
static volatile uint32_t ramWord = 0x12345678u;

static int32_t out[16]; static uint32_t spread[16];
static void show(int n) { for (int i = 0; i < n; i++) { printf(" %d", (int)out[i]); if (spread[i]) printf("+%u", (unsigned)spread[i]); } printf("\n"); }

/* P: two byte reads at 1F000000h, 0..12 no-ops between */
static void rowP(uint32_t e, uint32_t c) {
    write32(EXP1_DELAY, e); write32(COM_DELAY, c);
    for (int g = 0; g <= 12; g++) { gen(LBU, 2, g); timeit(0x1F000000u, 0); out[g] = (int32_t)lo - (int32_t)base0 - g; spread[g] = hi - lo; }
    restore();
    printf("P %08x %05x :", (unsigned)e, (unsigned)c); show(13);
}
/* X: a read of a, g no-ops, a read of b (g = 0 1 2 3 4 6 8) */
static void rowX(const char* name, uint32_t e, uint32_t c, int opA, uint32_t a, int opB, uint32_t b) {
    static const int gaps[7] = { 0, 1, 2, 3, 4, 6, 8 };
    write32(EXP1_DELAY, e); write32(COM_DELAY, c);
    for (int i = 0; i < 7; i++) { gen2(opA, gaps[i], opB); timeit(a, b); out[i] = (int32_t)lo - (int32_t)base0 - gaps[i]; spread[i] = hi - lo; }
    restore();
    printf("X %-12s %08x %05x :", name, (unsigned)e, (unsigned)c); show(7);
}
/* E: EXP2's register and COM_DELAY, reads at 1F802000h: b1 b4 h1 h4 w1 w4 */
static void rowE(uint32_t e, uint32_t c) {
    static const int ops[3] = { LBU, LHU, LW };
    write32(EXP2_DELAY, e); write32(COM_DELAY, c);
    const uint32_t eb = read32(EXP2_DELAY), cb = read32(COM_DELAY);
    int n = 0;
    for (int w = 0; w < 3; w++) for (int k = 1; k <= 4; k += 3) { gen(ops[w], k, 0); timeit(0x1F802000u, 0); out[n] = (int32_t)lo - (int32_t)base0; spread[n] = hi - lo; n++; }
    restore();
    printf("E %08x %05x :", (unsigned)eb, (unsigned)cb); show(6);
}

int main() {
    printf("\nbus/exp1-read2\n");
    printf("Expansion 1 again: how a gap takes its recovery up, across regions, EXP2's register, the cart's other addresses. Loads only.\n");
    printf("There are no assertions - compare against your emulator, and say which console and which cartridge ran it.\n\n");
    EnterCriticalSection();
    stamp_begin();
    (void)idle; (void)pr_s; (void)pr_u; (void)pr_d; (void)pr_x; (void)pr_nl;
    exp1Kernel = read32(EXP1_DELAY); exp2Kernel = read32(EXP2_DELAY); comKernel = read32(COM_DELAY);
    printf("EXP1_DELAY %08x EXP2_DELAY %08x COM_DELAY %08x as found\n", (unsigned)exp1Kernel, (unsigned)exp2Kernel, (unsigned)comKernel);
    gen(LW, 0, 0); timeit(0x1F000000u, 0); base0 = lo;
    printf("baseline (K = 0) %u\n\n", (unsigned)lo);

    printf("P exp1     com   : two byte reads at 1F000000, 0 1 2 .. 12 no-ops between (their own cycles taken off)\n");
    rowP(exp1Kernel, comKernel);
    rowP(0x00130030u, 0x00031125u);
    rowP(0x00130130u, 0x00031125u); rowP(0x00130130u, 0x00039C7Eu); rowP(0x00130130u, 0x0003FFFFu); rowP(0x00130130u, 0x00031128u);   /* recovery: COM0 5 E F 8 */
    rowP(0x00130430u, 0x00039C7Eu); rowP(0x00130430u, 0x0003FFFFu); rowP(0x00130430u, 0x00031625u);                                   /* floating: COM2 C F 6 */
    rowP(0x00130530u, 0x00031125u); rowP(0x00130530u, 0x00039C7Eu); rowP(0x00130530u, 0x00031628u);                                   /* both */
    rowP(0x00130830u, 0x00039C7Eu); rowP(0x00130D30u, 0x00039C7Eu);                                                                   /* pre-strobe; all three */
    rowP(0x001300F0u, 0x00031125u); rowP(0x001301F0u, 0x00039C7Eu);                                                                   /* a long read delay, alone and with recovery */
    rowP(exp1Kernel, 0x00031A2Au);

    printf("\nX pair         exp1     com   : a read, 0 1 2 3 4 6 8 no-ops, another read (the no-ops' cycles taken off)\n");
    const uint32_t ram = (uint32_t)&ramWord;
    static const uint32_t xs[3][2] = { { 0x00130030u, 0x00031125u }, { 0x00130130u, 0x00039C7Eu }, { 0x00130530u, 0x00039C7Eu } };
    for (int s = 0; s < 3; s++) {
        rowX("exp1,exp1", xs[s][0], xs[s][1], LBU, 0x1F000000u, LBU, 0x1F000100u);
        rowX("exp1,bios", xs[s][0], xs[s][1], LBU, 0x1F000000u, LBU, 0xBFC00000u);
        rowX("bios,exp1", xs[s][0], xs[s][1], LBU, 0xBFC00000u, LBU, 0x1F000000u);
        rowX("exp1,exp2", xs[s][0], xs[s][1], LBU, 0x1F000000u, LBU, 0x1F802000u);
        rowX("exp2,exp1", xs[s][0], xs[s][1], LBU, 0x1F802000u, LBU, 0x1F000000u);
        rowX("exp1,ram", xs[s][0], xs[s][1], LBU, 0x1F000000u, LW, ram);
        rowX("ram,exp1", xs[s][0], xs[s][1], LW, ram, LBU, 0x1F000000u);
        rowX("exp1h,exp1", xs[s][0], xs[s][1], LHU, 0x1F000000u, LBU, 0x1F000100u);
    }

    printf("\nE exp2     com   : b1 b4 h1 h4 w1 w4   at 1F802000\n");
    rowE(exp2Kernel, comKernel);
    for (uint32_t d = 0; d < 16; d++) rowE(0x00070000u | (d << 4), comKernel);
    static const uint32_t coms[2] = { 0x00031125u, 0x00039C7Eu };
    for (int c = 0; c < 2; c++) for (uint32_t m = 1; m < 16; m++) rowE(0x00070077u | (m << 8), coms[c]);
    rowE(0x00070777u, 0x00030000u); rowE(0x00070777u, 0x00033333u); rowE(0x00070777u, 0x0003FFFFu);
    rowE(0x00070277u, 0x0003FFFFu); rowE(0x00070270u, 0x0003FFFFu);
    rowE(0x00071070u, comKernel); rowE(0x00071777u, comKernel);

    printf("\nI base     : the bytes at base + 0 4 8 .. 3C (the kernel's settings)\n");
    static const uint32_t bases[] = { 0x1F020000u, 0x1F020040u, 0x1F021000u, 0x1F030000u, 0x1F03FFC0u, 0x1F060000u, 0x1F060040u, 0x1F061000u,
                                      0x1F068000u, 0x1F070000u, 0x1F078000u, 0x1F07FFC0u };
    for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        printf("I %08x :", (unsigned)bases[b]);
        for (uint32_t o = 0; o < 0x40; o += 4) printf(" %02x", (unsigned)read8(bases[b] + o));
        printf("\n");
    }
    printf("I2 1f060000 read four times: %02x %02x %02x %02x; 1f020010 %02x %02x; 1f020018 %02x %02x\n",
           (unsigned)read8(0x1F060000u), (unsigned)read8(0x1F060000u), (unsigned)read8(0x1F060000u), (unsigned)read8(0x1F060000u),
           (unsigned)read8(0x1F020010u), (unsigned)read8(0x1F020010u), (unsigned)read8(0x1F020018u), (unsigned)read8(0x1F020018u));

    restore();
    const bool ok = read32(EXP1_DELAY) == exp1Kernel && read32(EXP2_DELAY) == exp2Kernel && read32(COM_DELAY) == comKernel;
    printf("\nR EXP1_DELAY %08x EXP2_DELAY %08x COM_DELAY %08x at the end (%s)\n", (unsigned)read32(EXP1_DELAY), (unsigned)read32(EXP2_DELAY),
           (unsigned)read32(COM_DELAY), ok ? "as found" : "NOT AS FOUND");
    stamp_end();
    ExitCriticalSection();
    printf("\nbus/exp1-read2: Done.\n");
    for (;;) VSync(0);
}
