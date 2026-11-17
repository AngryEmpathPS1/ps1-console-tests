/*
 * bus/exp1-read - what a read of Expansion 1 costs and returns, on a real device (2026-11-16, the busValue session; asked by
 * main on AngryEmpath's go).
 *
 * WHY. Since 2026-11-16 the SCPH-1001 has AngryEmpath's GameShark in its parallel port with our cart boot ROM in its flash (an
 * SST 29EE020; cartrom/, STATUS "The first power-on snapshot from real hardware"): for the first time Expansion 1 (1F000000h)
 * has a chip that answers. This core's EXP1 timing has only ever come from nocash's formula (src/psx.js slowAccessB2B: the
 * region's Delay/Size register and COM_DELAY), fitted on the CD's and the SPU's rows and EXP2's; no console of ours ever timed
 * an EXP1 read.
 *
 * HARD RULE: NOT ONE STORE INTO 1F000000h-1F07FFFFh. A flash command sequence there could start a page write or an erase. The
 * timed code is generated here (gen()) and gen() can emit nothing but lb / lbu / lh / lhu / lw from the address it is given;
 * the only stores this program makes go to RAM, to Timer 2's mode, and to EXP1_DELAY (1F801008h) and COM_DELAY (1F801020h),
 * which are put back after every row and checked at the end (the last line says so).
 * NOT TOUCHED in EXP1_DELAY: bits 16-20 (the window's size: a smaller one is a bus error), bits 24-31 (DMA timing override, wide
 * DMA, the wait-for-device bit that could hold an access for ever).
 *
 * HOW IT TIMES. A routine written into RAM, the instruction cache flushed, then called seven times and the first call thrown
 * away (cold): [lhu Timer 2] [K loads of one width from one address, G no-ops between them] [lhu Timer 2]. The same routine
 * with K = 0 is the baseline; a row prints the SMALLEST of six warm calls less the baseline, and "+n" behind it when the
 * largest was n more (a DRAM refresh landing inside: each call starts 0-47 single cycles on, stamp.h's scatter). So K = 1 is
 * one read alone and (K = 4 less K = 1) / 3 a read right behind another. Interrupts off throughout.
 *
 * THE ROWS
 *   A addr      b1 b2 b4 b8  h1 h2 h4 h8  w1 w2 w4 w8     the kernel's settings; lbu / lhu / lw, K = 1 2 4 8
 *   G w gap     K = 4 reads with 1..5 no-ops between them (cycles less the no-ops' own)
 *   V addr      b h w   what the reads return (the kernel's settings; and Vw at the 16-bit bus setting)
 *   D exp1 com  b1 b4 h1 h4 w1 w4   at 1F000000h under that EXP1_DELAY and COM_DELAY
 *   N exp1 com  the same at 1F020000h (past the first chip) for a few of the settings
 *   R ...       the registers read back at the end
 * Addresses: the chip's first 128 KB (1F000000h), past it (1F020000h), the second window (1F040000h), the cart's I/O
 * (1F060000h), the window's last word (1F07FFFCh); EXP2 (1F802000h, nothing there: the control) and the BIOS ROM (BFC00000h).
 *
 * RUN IT HERE FIRST: PSX_VC_CART=cartrom/build/cart.bin PSX_MODEL=SCPH-1001 node tools/hwrunner.js exp1-read --emu ... (the
 * runner's locked start); PREDICTED.txt is that run on the core named in it.
 */
#include "../stamp-template/stamp.h"

#define EXP1_DELAY 0x1F801008u
#define COM_DELAY  0x1F801020u

typedef uint32_t (*TimedFn)(uint32_t addr);
static uint32_t code[160] __attribute__((aligned(64)));

enum { LB = 0x20, LH = 0x21, LW = 0x23, LBU = 0x24, LHU = 0x25 };

/* The kernel's FlushCache, A0h function 44h: every I-cache line invalid (bus-latch's, bios-window's). */
static void STAMP_NOINLINE FlushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

/* [lui t9,1F80h] [lhu v0,1120h(t9)] K x ([load t0,0(a0)] G x [nop]) [lhu v1,1120h(t9)] [jr ra] [subu v0,v1,v0]. Loads only. */
static void STAMP_NOINLINE gen(int op, int k, int gap) {
    if (op != LB && op != LH && op != LW && op != LBU && op != LHU) { printf("gen: not a load (%x): refused\n", op); for (;;) {} }
    int n = 0;
    code[n++] = 0x3C191F80u;
    code[n++] = 0x97221120u;                                         /* lhu v0, 1120h(t9) */
    for (int i = 0; i < k; i++) {
        code[n++] = ((uint32_t)op << 26) | (4u << 21) | (8u << 16);   /* load t0, 0(a0) */
        for (int g = 0; g < gap && i + 1 < k; g++) code[n++] = 0;
    }
    code[n++] = 0x97231120u;                                         /* lhu v1, 1120h(t9) */
    code[n++] = 0x03E00008u;                                         /* jr ra */
    code[n++] = 0x00621023u;                                         /* subu v0, v1, v0 */
    FlushCache();
}
static uint32_t lo, hi;
static void STAMP_NOINLINE timeit(uint32_t addr) {
    TimedFn f = (TimedFn)(uint32_t)code;
    lo = 0xFFFF; hi = 0;
    (void)f(addr);                                                    /* cold: the lines fill */
    for (int r = 0; r < 6; r++) {
        scatter(rnd(48));
        const uint32_t d = f(addr) & 0xFFFF;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
}
static uint32_t base0;
static void cell(int op, int k, int gap, uint32_t addr) {
    gen(op, k, gap); timeit(addr);
    const int32_t c = (int32_t)lo - (int32_t)base0 - gap * (k > 0 ? k - 1 : 0);
    printf(" %d", (int)c);
    if (hi != lo) printf("+%u", (unsigned)(hi - lo));
}

static uint32_t exp1Kernel, comKernel;
static void setRegs(uint32_t e, uint32_t c) { write32(EXP1_DELAY, e); write32(COM_DELAY, c); }
static void restore() { write32(EXP1_DELAY, exp1Kernel); write32(COM_DELAY, comKernel); }

static void rowD(char tag, uint32_t e, uint32_t c, uint32_t addr) {
    uint32_t v[6]; uint32_t s[6]; int n = 0;
    static const int ops[3] = { LBU, LHU, LW };
    setRegs(e, c);
    const uint32_t eb = read32(EXP1_DELAY), cb = read32(COM_DELAY);
    for (int w = 0; w < 3; w++) for (int k = 1; k <= 4; k += 3) { gen(ops[w], k, 0); timeit(addr); v[n] = lo - base0; s[n] = hi - lo; n++; }
    restore();                                                        /* before a byte is printed */
    printf("%c %08x %05x :", tag, (unsigned)eb, (unsigned)cb);
    for (int i = 0; i < 6; i++) { printf(" %d", (int)(int32_t)v[i]); if (s[i]) printf("+%u", (unsigned)s[i]); }
    printf("\n");
}

int main() {
    printf("\nbus/exp1-read\n");
    printf("What a read of Expansion 1 costs and returns: K reads between two Timer 2 reads, less the K = 0 routine. Loads only.\n");
    printf("There are no assertions - compare against your emulator, and say which console and which cartridge ran it.\n\n");
    EnterCriticalSection();
    stamp_begin();
    (void)idle; (void)pr_s; (void)pr_u; (void)pr_d; (void)pr_x; (void)pr_nl;   /* stamp.h's helpers this test has no use for */
    exp1Kernel = read32(EXP1_DELAY); comKernel = read32(COM_DELAY);
    printf("EXP1_DELAY %08x COM_DELAY %08x as found\n", (unsigned)exp1Kernel, (unsigned)comKernel);
    gen(LW, 0, 0); timeit(0x1F000000u); base0 = lo;
    printf("baseline (K = 0) %u", (unsigned)lo); if (hi != lo) printf("+%u", (unsigned)(hi - lo)); printf("\n\n");

    static const uint32_t addrs[] = { 0x1F000000u, 0x1F000084u, 0x1F01FFFCu, 0x1F020000u, 0x1F03FFFCu, 0x1F040000u, 0x1F05FFFCu,
                                      0x1F060000u, 0x1F060010u, 0x1F07FFFCu, 0x1F802000u, 0xBFC00000u };
    static const int ops[3] = { LBU, LHU, LW };
    printf("A addr      b1 b2 b4 b8 | h1 h2 h4 h8 | w1 w2 w4 w8\n");
    for (unsigned a = 0; a < sizeof(addrs) / sizeof(addrs[0]); a++) {
        printf("A %08x", (unsigned)addrs[a]);
        for (int w = 0; w < 3; w++) { for (int k = 1; k <= 8; k *= 2) cell(ops[w], k, 0, addrs[a]); if (w < 2) printf(" |"); }
        printf("\n");
    }
    printf("\nA' signed loads (lb, lh) at 1F000000: b1 b4 | h1 h4\nA' 1f000000");
    cell(LB, 1, 0, 0x1F000000u); cell(LB, 4, 0, 0x1F000000u); printf(" |"); cell(LH, 1, 0, 0x1F000000u); cell(LH, 4, 0, 0x1F000000u);
    printf("\n\nG width: 4 reads at 1F000000 with 0..5 no-ops between (the no-ops' own cycles taken off)\n");
    for (int w = 0; w < 3; w++) { printf("G %c", "bhw"[w]); for (int g = 0; g <= 5; g++) cell(ops[w], 4, g, 0x1F000000u); printf("\n"); }
    printf("G2 width: the same at 1F802000 (EXP2)\n");
    for (int w = 0; w < 3; w++) { printf("G2 %c", "bhw"[w]); for (int g = 0; g <= 5; g++) cell(ops[w], 4, g, 0x1F802000u); printf("\n"); }

    printf("\nV addr      byte half word (the kernel's settings)\n");
    static const uint32_t vaddrs[] = { 0x1F000000u, 0x1F000004u, 0x1F000080u, 0x1F000084u, 0x1F01FFFCu, 0x1F020000u, 0x1F020084u, 0x1F03FFFCu,
                                       0x1F040000u, 0x1F040084u, 0x1F05FFFCu, 0x1F060000u, 0x1F060004u, 0x1F060008u, 0x1F060010u, 0x1F060018u,
                                       0x1F060020u, 0x1F060030u, 0x1F070000u, 0x1F07FFFCu, 0x1F802000u, 0x1F802020u, 0x1F802040u };
    for (unsigned a = 0; a < sizeof(vaddrs) / sizeof(vaddrs[0]); a++)
        printf("V %08x %02x %04x %08x\n", (unsigned)vaddrs[a], (unsigned)read8(vaddrs[a]), (unsigned)read16(vaddrs[a]), (unsigned)read32(vaddrs[a]));
    {   /* the 16-bit bus bit on an 8-bit chip: what a halfword and a word then are */
        setRegs((exp1Kernel | 0x1000u), comKernel);
        uint32_t h[4], w[4]; static const uint32_t a16[4] = { 0x1F000000u, 0x1F000084u, 0x1F020000u, 0x1F060000u };
        for (int i = 0; i < 4; i++) { h[i] = read16(a16[i]); w[i] = read32(a16[i]); }
        restore();
        for (int i = 0; i < 4; i++) printf("Vw %08x ---- %04x %08x (EXP1_DELAY bit 12 set)\n", (unsigned)a16[i], (unsigned)h[i], (unsigned)w[i]);
    }

    printf("\nD exp1     com   : b1 b4 h1 h4 w1 w4   at 1F000000\n");
    rowD('D', exp1Kernel, comKernel, 0x1F000000u);
    for (uint32_t d = 0; d < 16; d++) rowD('D', 0x00130000u | (d << 4), comKernel, 0x1F000000u);              /* the read delay */
    rowD('D', 0x00130031u, comKernel, 0x1F000000u); rowD('D', 0x0013003Fu, comKernel, 0x1F000000u);             /* the write delay: no part of a read? */
    static const uint32_t coms[2] = { 0x00031125u, 0x00039C7Eu };                                              /* COM3..0 = 1 1 2 5 and 9 C 7 E */
    for (int c = 0; c < 2; c++) for (uint32_t wd = 0; wd <= 0xF; wd += 0xF) for (uint32_t m = 1; m < 16; m++)   /* bits 8-11, every set, write delay 0 and F */
        rowD('D', 0x00130030u | (m << 8) | wd, coms[c], 0x1F000000u);
    static const uint32_t coms2[3] = { 0x00030000u, 0x00033333u, 0x0003FFFFu };
    for (int c = 0; c < 3; c++) for (uint32_t m = 1; m < 16; m <<= 1) rowD('D', 0x0013003Fu | (m << 8), coms2[c], 0x1F000000u);   /* each bit alone, COMs 0 3 F */
    rowD('D', 0x00131000u, comKernel, 0x1F000000u); rowD('D', 0x00131030u, comKernel, 0x1F000000u);             /* the 16-bit bus */
    rowD('D', 0x001310F0u, comKernel, 0x1F000000u); rowD('D', exp1Kernel | 0x1000u, comKernel, 0x1F000000u);
    rowD('D', exp1Kernel ^ 0x2000u, comKernel, 0x1F000000u);                                                    /* bit 13 (auto-increment) off */
    for (uint32_t c = 0; c < 16; c += 5) rowD('D', exp1Kernel, 0x00031020u | c | (c << 8), 0x1F000000u);        /* the kernel's register, COM0 = COM2 = 0 5 A F */

    printf("\nN exp1     com   : b1 b4 h1 h4 w1 w4   at 1F020000 (past the first chip) and 1F060000 (the cart's I/O)\n");
    static const uint32_t nset[4][2] = { { 0, 0 }, { 0x00130000u, 0x00031125u }, { 0x001300F0u, 0x00031125u }, { 0x00130F3Fu, 0x00039C7Eu } };
    for (int i = 0; i < 4; i++) rowD('N', i ? nset[i][0] : exp1Kernel, i ? nset[i][1] : comKernel, 0x1F020000u);
    for (int i = 0; i < 4; i++) rowD('M', i ? nset[i][0] : exp1Kernel, i ? nset[i][1] : comKernel, 0x1F060000u);

    restore();
    printf("\nR EXP1_DELAY %08x COM_DELAY %08x at the end (%s)\n", (unsigned)read32(EXP1_DELAY), (unsigned)read32(COM_DELAY),
           read32(EXP1_DELAY) == exp1Kernel && read32(COM_DELAY) == comKernel ? "as found" : "NOT AS FOUND");
    stamp_end();
    ExitCriticalSection();
    printf("\nbus/exp1-read: Done.\n");
    for (;;) VSync(0);
}
