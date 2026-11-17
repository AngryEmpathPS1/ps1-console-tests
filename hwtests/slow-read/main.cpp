/*
 * bus/slow-read - the CD's and the SPU's registers, the BIOS ROM and Expansion 1 read under the delay settings games write
 * (2026-11-16, the busValue session; main's ask on AngryEmpath's go, after bus/exp1-read and exp1-read2).
 *
 * WHY. exp1-read on the SCPH-1001 showed the Delay/Size register's periods are not nocash's formula on EXP1 and EXP2: recovery
 * (bit 8) needs no bit 0, nothing costs a read alone, the next read waits as a time. The CD (1F801800h) and the SPU (1F801C00h+)
 * are the regions games retime - Resident Evil rewrites CD_DELAY 00020843h <-> 00020943h and COM_DELAY 0132Ch / 01325h / 01323h
 * thousands of times a boot (STATUS M129) - and this core has them from hwtests/mem-delay, a loop with instructions between its
 * accesses (v0.377.35: COM0 - 1 a further bus cycle, max(0, COM0 - 6) "once an access"). This times them as exp1-read timed
 * EXP1: one read alone, four back to back, two with a gap, and pairs across regions.
 *   T  b1 b4 h1 h4 w1 w4 at the CD, the SPU (halfwords at 1F801DAAh, words at 1F801DA8h) and the BIOS ROM under six COM_DELAY
 *      values (the kernel's, the three games write, two that part COM0 from COM2), the CD at both CD_DELAY values
 *   P  two reads with 0..10 no-ops between: how an idle gap takes the wait up
 *   X  pairs across regions, and mixed sizes inside Expansion 1
 *   L  the bus latch (I_MASK's upper half) behind a read of each region
 * ONLY LOADS are generated (gen / gen2 refuse anything else): not one store into 1F000000h-1F07FFFFh; a word read at 1F801800h
 * also reads 1801h-1803h (an empty response FIFO, the data port, the interrupt flags), as hwtests/mem-delay's did. Stores: RAM,
 * Timer 2's mode, EXP1_DELAY, CD_DELAY (the two values games write) and COM_DELAY, each put back before its row prints and
 * checked at the end. SPU_DELAY, BIOS_DELAY and EXP2_DELAY are not written.
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

#define CD_DELAY 0x1F801018u
#define SPU_DELAY 0x1F801014u
#define BIOS_DELAY 0x1F801010u
#define I_MASK 0x1F801074u
static uint32_t cdKernel;
static void restoreAll() { restore(); write32(CD_DELAY, cdKernel); }
static int32_t out[16]; static uint32_t spread[16];
static void show(int n) { for (int i = 0; i < n; i++) { printf(" %d", (int)out[i]); if (spread[i]) printf("+%u", (unsigned)spread[i]); } printf("\n"); }
static void set(uint32_t e1, uint32_t cd, uint32_t com) { write32(EXP1_DELAY, e1); write32(CD_DELAY, cd); write32(COM_DELAY, com); }

/* T: b1 b4 h1 h4 w1 w4 at one address under one setting */
static void rowT(const char* name, uint32_t addr, uint32_t e1, uint32_t cd, uint32_t com) {
    static const int ops[3] = { LBU, LHU, LW };
    set(e1, cd, com);
    int n = 0;
    for (int w = 0; w < 3; w++) for (int k = 1; k <= 4; k += 3) { gen(ops[w], k, 0); timeit(w == 2 ? addr & ~3u : addr, 0); out[n] = (int32_t)lo - (int32_t)base0; spread[n] = hi - lo; n++; }   /* a word from its aligned address (1F801DAAh: an address error otherwise - the emulator run caught it) */
    restoreAll();
    printf("T %-5s cd %05x com %05x :", name, (unsigned)cd, (unsigned)com); show(6);
}
/* P: two reads of one width at one address, 0..10 no-ops between */
static void rowP(const char* name, int op, uint32_t addr, uint32_t e1, uint32_t cd, uint32_t com) {
    set(e1, cd, com);
    for (int g = 0; g <= 10; g++) { gen(op, 2, g); timeit(addr, 0); out[g] = (int32_t)lo - (int32_t)base0 - g; spread[g] = hi - lo; }
    restoreAll();
    printf("P %-5s cd %05x com %05x :", name, (unsigned)cd, (unsigned)com); show(11);
}
/* X: a read of a, g no-ops, a read of b (g = 0 1 2 3 4 6 8) */
static void rowX(const char* name, uint32_t e1, uint32_t cd, uint32_t com, int opA, uint32_t a, int opB, uint32_t b) {
    static const int gaps[7] = { 0, 1, 2, 3, 4, 6, 8 };
    set(e1, cd, com);
    for (int i = 0; i < 7; i++) { gen2(opA, gaps[i], opB); timeit(a, b); out[i] = (int32_t)lo - (int32_t)base0 - gaps[i]; spread[i] = hi - lo; }
    restoreAll();
    printf("X %-11s e1 %08x cd %05x com %05x :", name, (unsigned)e1, (unsigned)cd, (unsigned)com); show(7);
}

int main() {
    printf("\nbus/slow-read\n");
    printf("The CD's and the SPU's registers, the BIOS ROM and Expansion 1 read under the delay settings games write. Loads only.\n");
    printf("There are no assertions - compare against your emulator, and say which console and which cartridge ran it.\n\n");
    EnterCriticalSection();
    stamp_begin();
    (void)idle; (void)pr_s; (void)pr_u; (void)pr_d; (void)pr_x; (void)pr_nl;
    exp1Kernel = read32(EXP1_DELAY); exp2Kernel = read32(EXP2_DELAY); comKernel = read32(COM_DELAY); cdKernel = read32(CD_DELAY);
    printf("EXP1 %08x BIOS %08x SPU %08x CD %08x EXP2 %08x COM %08x as found\n", (unsigned)exp1Kernel, (unsigned)read32(BIOS_DELAY),
           (unsigned)read32(SPU_DELAY), (unsigned)cdKernel, (unsigned)exp2Kernel, (unsigned)comKernel);
    gen(LW, 0, 0); timeit(0x1F000000u, 0); base0 = lo;
    printf("baseline (K = 0) %u\n\n", (unsigned)lo);
    const uint32_t CD = 0x1F801800u, SPU = 0x1F801DAAu, SPU4 = 0x1F801DA8u, BIOS = 0xBFC00000u, E1 = 0x1F000000u, ram = (uint32_t)&ramWord;
    static const uint32_t coms[6] = { 0x00031125u, 0x00001323u, 0x00001325u, 0x0000132Cu, 0x00039C7Eu, 0x0003168Fu };
    static const uint32_t cds[2] = { 0x00020843u, 0x00020943u };

    printf("T region cd com : b1 b4 h1 h4 w1 w4 (the CD at 1F801800, the SPU at 1F801DAA / words at 1F801DA8, the BIOS at BFC00000)\n");
    for (int c = 0; c < 6; c++) {
        for (int d = 0; d < 2; d++) rowT("cd", CD, exp1Kernel, cds[d], coms[c]);
        rowT("spu", SPU, exp1Kernel, cdKernel, coms[c]);
        rowT("spu4", SPU4, exp1Kernel, cdKernel, coms[c]);
        rowT("bios", BIOS, exp1Kernel, cdKernel, coms[c]);
    }
    printf("\nP region cd com : two reads, 0 1 .. 10 no-ops between (their own cycles taken off)\n");
    for (int c = 0; c < 6; c++) {
        rowP("cd", LBU, CD, exp1Kernel, cds[1], coms[c]);
        rowP("spu", LHU, SPU, exp1Kernel, cdKernel, coms[c]);
        rowP("bios", LBU, BIOS, exp1Kernel, cdKernel, coms[c]);
    }
    rowP("cd", LBU, CD, exp1Kernel, cds[0], coms[3]); rowP("cd", LBU, CD, exp1Kernel, cds[0], coms[4]);

    printf("\nX pair e1 cd com : a read, 0 1 2 3 4 6 8 no-ops, another read\n");
    static const uint32_t xc[3] = { 0x00031125u, 0x0000132Cu, 0x0003168Fu };
    for (int c = 0; c < 3; c++) {
        rowX("cd,spu", exp1Kernel, cds[1], xc[c], LBU, CD, LHU, SPU);
        rowX("spu,cd", exp1Kernel, cds[1], xc[c], LHU, SPU, LBU, CD);
        rowX("cd,bios", exp1Kernel, cds[1], xc[c], LBU, CD, LBU, BIOS);
        rowX("bios,cd", exp1Kernel, cds[1], xc[c], LBU, BIOS, LBU, CD);
        rowX("spu,bios", exp1Kernel, cds[1], xc[c], LHU, SPU, LBU, BIOS);
        rowX("bios,spu", exp1Kernel, cds[1], xc[c], LBU, BIOS, LHU, SPU);
        rowX("spu,spu", exp1Kernel, cds[1], xc[c], LHU, SPU, LHU, SPU4);
        rowX("exp1,cd", exp1Kernel, cds[1], xc[c], LBU, E1, LBU, CD);
        rowX("cd,exp1", exp1Kernel, cds[1], xc[c], LBU, CD, LBU, E1);
        rowX("bios,bios", exp1Kernel, cds[1], xc[c], LBU, BIOS, LBU, BIOS + 0x100);
        rowX("cd,ram", exp1Kernel, cds[1], xc[c], LBU, CD, LW, ram);
        rowX("ram,cd", exp1Kernel, cds[1], xc[c], LW, ram, LBU, CD);
    }
    /* mixed sizes in Expansion 1: kernel, recovery, recovery + floating */
    static const uint32_t ms[3][2] = { { 0, 0 }, { 0x00130130u, 0x00039C7Eu }, { 0x00130530u, 0x0003168Fu } };
    for (int s = 0; s < 3; s++) {
        const uint32_t e = s ? ms[s][0] : exp1Kernel, c = s ? ms[s][1] : comKernel;
        rowX("e1 b,w", e, cdKernel, c, LBU, E1, LW, E1 + 0x100);
        rowX("e1 w,b", e, cdKernel, c, LW, E1, LBU, E1 + 0x100);
        rowX("e1 h,w", e, cdKernel, c, LHU, E1, LW, E1 + 0x100);
        rowX("e1 w,w", e, cdKernel, c, LW, E1, LW, E1 + 0x100);
        rowX("e1,bios", e, cdKernel, c, LBU, E1, LBU, BIOS);
        rowX("bios,e1", e, cdKernel, c, LBU, BIOS, LBU, E1);
        rowX("e1,exp2", e, cdKernel, c, LBU, E1, LBU, 0x1F802000u);
        rowX("exp2,e1", e, cdKernel, c, LBU, 0x1F802000u, LBU, E1);
    }

    /* L: the bus latch (I_MASK's upper half) behind a read of each region - bv2 had the BIOS ROM leave it; Expansion 1? */
    printf("\nL what was read, then lw I_MASK at once: the value read, I_MASK as a word\n");
    static const uint32_t la[6] = { 0x1F000084u, 0x1F020000u, 0x1F060000u, 0x1F802000u, 0xBFC00000u, 0x1F801800u };
    volatile uint32_t* rp = &ramWord;
    for (int i = 0; i < 6; i++) {
        const uint32_t prime = *rp;                                 /* the latch set by a RAM read first */
        const uint32_t v = read32(la[i]); const uint32_t m = read32(I_MASK);
        printf("L %08x %08x %08x (RAM word before: %08x)\n", (unsigned)la[i], (unsigned)v, (unsigned)m, (unsigned)prime);
    }

    restoreAll();
    const bool ok = read32(EXP1_DELAY) == exp1Kernel && read32(EXP2_DELAY) == exp2Kernel && read32(COM_DELAY) == comKernel && read32(CD_DELAY) == cdKernel;
    printf("\nR EXP1 %08x EXP2 %08x CD %08x COM %08x at the end (%s)\n", (unsigned)read32(EXP1_DELAY), (unsigned)read32(EXP2_DELAY),
           (unsigned)read32(CD_DELAY), (unsigned)read32(COM_DELAY), ok ? "as found" : "NOT AS FOUND");
    stamp_end();
    ExitCriticalSection();
    printf("\nbus/slow-read: Done.\n");
    for (;;) VSync(0);
}
