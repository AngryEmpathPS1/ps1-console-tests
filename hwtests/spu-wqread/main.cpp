/*
 * spu/spu-wqread - WHEN A READ BEHIND A RUN OF QUEUED STORES IS TAKEN, AND WHEN THE CPU GOES ON, to the cycle (2026-11-25, the SPU
 * session). hwtests/spu-wqland's read-side twin.
 *
 * WHY. SPU_STORE_TURNS' KNOWN-OPEN, the read's half: 39 warm cells of the console fuzzer's spuwprobe rows (an on-die read behind
 * RAM stores mixed with SPU ones) and `sw x3 lhu-spu` (45 cycles on the consoles, 43 here). hwtests/spu-wqland fixed where each
 * queued STORE lands (32 of 32 runs); with those known, no rule for where the read is let in fits all the fuzzer's rows (59 of 66 at
 * best), because a row gives one number - the cost of the whole run. This gives three per run.
 *
 * WHAT IT DOES. Interrupts off, sound muted, Timer 2 the system clock. A run starts at an exact Timer 2 count (hwtests/spu-wqland's
 * run_at: the last stretch counted into a sled of nops).
 *   A run : d1/d2 x8      the run ends with TWO Timer 2 reads back to back: d1 = the first read's value less the run's start (the
 *                         cycle the read is TAKEN, plus a constant: the row `L L` is the reference), d2 = the second's less the
 *                         first's (when the CPU went on, plus the second read's own wait). Eight trials at scattered starts
 *                         (a DRAM refresh inside one shows as an outlier). A run ending `P L`: P is a load from the SPU
 *                         (SPUSTAT), d1 the Timer 2 read behind it, d2 0.
 *   Q run : e1 e2         the run ends with a Timer 2 read and then E, the store that turns the SPU IRQ on: the last start cycle
 *                         for which E still makes voice 12's slot of the sample (spu-wqland's edge; `L E` is the reference) -
 *                         when a STORE issued right behind the read lands
 * Runs (S = sh to an SPU voice's volume; W = sw there; R = sh to RAM, one row; n = a nop; L = lhu Timer 2; P = lhu SPUSTAT;
 * E = sh SPUCNT with bit 6). No assertions. Every wait is bounded. THE RULE: run here first:
 * `node hwtests/run.js spu-wqread --ntsc --frames=6000 --quiet=4000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static volatile uint16_t sink[8] __attribute__((aligned(16)));

/* run_at(target, ram, enable, tail): hwtests/spu-wqland's, returning two registers: the low word 0 (-1: the target had passed), the
 * high word $v1 = the tail's first Timer 2 read | its second << 16. */
extern "C" { unsigned long long run_at(uint32_t target, volatile uint16_t* ram, uint32_t enable, const void* tail); }
#define S_ "  sh $t6, 0x1c00($t0)\n"
#define S2 "  sh $t6, 0x1c02($t0)\n"
#define S3 "  sh $t6, 0x1c10($t0)\n"
#define W_ "  sw $t6, 0x1c00($t0)\n"
#define W2 "  sw $t6, 0x1c10($t0)\n"
#define W3 "  sw $t6, 0x1c20($t0)\n"
#define R_ "  sh $t6, 0($a1)\n"
#define R2 "  sh $t6, 2($a1)\n"
#define R3 "  sh $t6, 4($a1)\n"
#define N_ "  nop\n"
#define L_ "  lhu $v1, 0x1120($t0)\n"
#define M_ "  lhu $t7, 0x1120($t0)\n"
#define P_ "  lhu $t8, 0x1dae($t0)\n"
#define E_ "  sh $a2, 0x1daa($t0)\n"
#define TAIL(name, body) ".globl tl_" name "\ntl_" name ":\n" body "  nop\n  sll $t7, $t7, 16\n  or $v1, $v1, $t7\n  jr $ra\n  move $v0, $zero\n"
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl run_at\n.type run_at,@function\nrun_at:\n"
    "  lui $t0, 0x1f80\n"
    "  andi $a0, $a0, 0xffff\n"
    "  move $t6, $zero\n"
    "  move $t7, $zero\n"
    "  move $v1, $zero\n"
    "1:\n"
    "  lhu $t1, 0x1120($t0)\n"
    "  nop\n"
    "  subu $t2, $a0, $t1\n"
    "  andi $t2, $t2, 0xffff\n"
    "  sltiu $t3, $t2, 0x4000\n"
    "  beqz $t3, 9f\n"
    "  sltiu $t3, $t2, 96\n"
    "  beqz $t3, 1b\n"
    "  nop\n"
    "  addiu $t2, $t2, -40\n"
    "  sll $t2, $t2, 2\n"
    "  la $t4, 8f\n"
    "  subu $t4, $t4, $t2\n"
    "  jr $t4\n"
    "  nop\n"
    "  .rept 64\n  nop\n  .endr\n"
    "8:\n"
    "  jr $a3\n"
    "  nop\n"
    "9:\n"
    "  jr $ra\n  li $v0, -1\n"
    /* A: stores, then two Timer 2 reads */
    TAIL("a_ll", L_ M_)
    TAIL("a_s", S_ L_ M_) TAIL("a_ss", S_ S2 L_ M_) TAIL("a_sss", S_ S2 S3 L_ M_)
    TAIL("a_r", R_ L_ M_) TAIL("a_rr", R_ R2 L_ M_) TAIL("a_rrr", R_ R2 R3 L_ M_)
    TAIL("a_rs", R_ S_ L_ M_) TAIL("a_rss", R_ S_ S2 L_ M_) TAIL("a_rrs", R_ R2 S_ L_ M_)
    TAIL("a_sr", S_ R_ L_ M_) TAIL("a_srr", S_ R_ R2 L_ M_) TAIL("a_ssr", S_ S2 R_ L_ M_)
    TAIL("a_srs", S_ R_ S2 L_ M_) TAIL("a_rsr", R_ S_ R2 L_ M_)
    TAIL("a_sn1", S_ N_ L_ M_) TAIL("a_sn2", S_ N_ N_ L_ M_) TAIL("a_sn4", S_ N_ N_ N_ N_ L_ M_) TAIL("a_sn8", S_ N_ N_ N_ N_ N_ N_ N_ N_ L_ M_)
    TAIL("a_ssn2", S_ S2 N_ N_ L_ M_) TAIL("a_rsn2", R_ S_ N_ N_ L_ M_) TAIL("a_srn2", S_ R_ N_ N_ L_ M_)
    TAIL("a_w", W_ L_ M_) TAIL("a_ww", W_ W2 L_ M_) TAIL("a_www", W_ W2 W3 L_ M_)
    /* A: a load from the SPU, then one Timer 2 read */
    TAIL("a_p", P_ L_) TAIL("a_sp", S_ P_ L_) TAIL("a_ssp", S_ S2 P_ L_) TAIL("a_rp", R_ P_ L_) TAIL("a_rsp", R_ S_ P_ L_) TAIL("a_srp", S_ R_ P_ L_)
    TAIL("a_wp", W_ P_ L_) TAIL("a_wwp", W_ W2 P_ L_) TAIL("a_wwwp", W_ W2 W3 P_ L_)
    /* Q: stores, a Timer 2 read (or an SPU load), then the enable */
    TAIL("q_l", L_ E_) TAIL("q_s", S_ L_ E_) TAIL("q_ss", S_ S2 L_ E_) TAIL("q_sss", S_ S2 S3 L_ E_)
    TAIL("q_rs", R_ S_ L_ E_) TAIL("q_sr", S_ R_ L_ E_) TAIL("q_ssr", S_ S2 R_ L_ E_) TAIL("q_rrs", R_ R2 S_ L_ E_) TAIL("q_srs", S_ R_ S2 L_ E_)
    TAIL("q_p", P_ E_) TAIL("q_sp", S_ P_ E_) TAIL("q_wwwp", W_ W2 W3 P_ E_)
    ".set pop\n");
#define X(n) extern "C" { extern const char tl_##n[]; }
X(a_ll) X(a_s) X(a_ss) X(a_sss) X(a_r) X(a_rr) X(a_rrr) X(a_rs) X(a_rss) X(a_rrs) X(a_sr) X(a_srr) X(a_ssr) X(a_srs) X(a_rsr)
X(a_sn1) X(a_sn2) X(a_sn4) X(a_sn8) X(a_ssn2) X(a_rsn2) X(a_srn2) X(a_w) X(a_ww) X(a_www)
X(a_p) X(a_sp) X(a_ssp) X(a_rp) X(a_rsp) X(a_srp) X(a_wp) X(a_wwp) X(a_wwwp)
X(q_l) X(q_s) X(q_ss) X(q_sss) X(q_rs) X(q_sr) X(q_ssr) X(q_rrs) X(q_srs) X(q_p) X(q_sp) X(q_wwwp)
struct Run { const char* name; const void* tail; };
static const Run ARUNS[] = {
    { "L L", tl_a_ll }, { "S L L", tl_a_s }, { "S S L L", tl_a_ss }, { "S S S L L", tl_a_sss },
    { "R L L", tl_a_r }, { "R R L L", tl_a_rr }, { "R R R L L", tl_a_rrr },
    { "R S L L", tl_a_rs }, { "R S S L L", tl_a_rss }, { "R R S L L", tl_a_rrs },
    { "S R L L", tl_a_sr }, { "S R R L L", tl_a_srr }, { "S S R L L", tl_a_ssr }, { "S R S L L", tl_a_srs }, { "R S R L L", tl_a_rsr },
    { "S n1 L L", tl_a_sn1 }, { "S n2 L L", tl_a_sn2 }, { "S n4 L L", tl_a_sn4 }, { "S n8 L L", tl_a_sn8 },
    { "S S n2 L L", tl_a_ssn2 }, { "R S n2 L L", tl_a_rsn2 }, { "S R n2 L L", tl_a_srn2 },
    { "W L L", tl_a_w }, { "W W L L", tl_a_ww }, { "W W W L L", tl_a_www },
    { "P L", tl_a_p }, { "S P L", tl_a_sp }, { "S S P L", tl_a_ssp }, { "R P L", tl_a_rp }, { "R S P L", tl_a_rsp }, { "S R P L", tl_a_srp },
    { "W P L", tl_a_wp }, { "W W P L", tl_a_wwp }, { "W W W P L", tl_a_wwwp },
    { "L L", tl_a_ll },
};
static const Run QRUNS[] = {
    { "L E", tl_q_l }, { "S L E", tl_q_s }, { "S S L E", tl_q_ss }, { "S S S L E", tl_q_sss },
    { "R S L E", tl_q_rs }, { "S R L E", tl_q_sr }, { "S S R L E", tl_q_ssr }, { "R R S L E", tl_q_rrs }, { "S R S L E", tl_q_srs },
    { "P E", tl_q_p }, { "S P E", tl_q_sp }, { "W W W P E", tl_q_wwwp },
    { "L E", tl_q_l },
};
#define NA (sizeof(ARUNS) / sizeof(ARUNS[0]))
#define NQ (sizeof(QRUNS) / sizeof(QRUNS[0]))

static uint32_t base;
static int16_t ad[NA][8][2];
static void readTimes(unsigned r) {
    const void* tail = ARUNS[r].tail;
    for (int k = -3; k < 8; k++) {
        int16_t d1 = -1, d2 = -1;
        for (int tries = 0; tries < 4; tries++) {
            idle(1500u + (uint32_t)((k + 3) * 211));                      /* scattered against the DRAM's refresh */
            const uint32_t target = now() + 2000u;
            while ((int32_t)(target - now()) > 700) {}
            const unsigned long long ret = run_at(target & 0xFFFF, sink, cnt, tail);
            now();
            if ((uint32_t)ret != 0) continue;
            const uint32_t v = (uint32_t)(ret >> 32);
            d1 = (int16_t)(((v & 0xFFFF) - (target & 0xFFFF)) & 0xFFFF);
            d2 = (v >> 16) ? (int16_t)(((v >> 16) - (v & 0xFFFF)) & 0xFFFF) : 0;
            break;
        }
        if (k >= 0) { ad[r][k][0] = d1; ad[r][k][1] = d2; }
    }
}
/* hwtests/spu-wqland's trial and edge */
static int trial(const void* tail, uint32_t phi) {
    for (int tries = 0; tries < 4; tries++) {
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
        idle(SAMPLE * 2u);
        write32(I_STAT, ~0x200u);
        const uint32_t n = (now() - base) / SAMPLE + 3, target = base + n * SAMPLE + phi;
        while ((int32_t)(target - now()) > 700) {}
        const uint32_t r = (uint32_t)run_at(target & 0xFFFF, sink, (uint32_t)(cnt | 0x0040), tail);
        while ((int32_t)(target + 450u - now()) > 0) {}
        const uint32_t i = read32(I_STAT);
        now();
        if (r == 0) return (i & 0x200u) ? 1 : 0;
    }
    return -1;
}
static int edgeOf(const void* tail) {
    for (int w = 0; w < 3; w++) trial(tail, 0);
    int o[16];
    for (int i = 0; i < 16; i++) o[i] = trial(tail, (uint32_t)i * 48u);
    for (int i = 0; i < 16; i++) {
        if (o[i] != 1 || o[(i + 1) & 15] != 0) continue;
        uint32_t lo = (uint32_t)i * 48u, hi = lo + 48u;
        while (hi - lo > 1) { const uint32_t mid = lo + (hi - lo) / 2; const int r = trial(tail, mid % SAMPLE); if (r == 1) lo = mid; else hi = mid; }
        return (int)(lo % SAMPLE);
    }
    return -1;
}

int main() {
    printf("\nspu/spu-wqread\n");
    printf("When a read behind queued stores is taken and the CPU goes on: A run : d1/d2 x8 (cycles); Q run : edge edge. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014), comDelay = read32(0x1F801020), cdDelay = read32(0x1F801018), ramSize = read32(0x1F801060);
    write32(0x1F801014, 0x200931E1); write32(0x1F801020, 0x00031125);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    idle(4000);
    write16(SPU(0x1A6), 0x1000 / 8);
    write16(SPU(0x1A8), 0x0700); for (int i = 0; i < 7; i++) write16(SPU(0x1A8), 0);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0010));
    for (int i = 0; i < 200000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
    idle(20000);
    write16(SPU(0x1AA), cnt);
    idle(4000);
    write16(SPU(0x1A6), 0x1800 / 8);
    write16(SPU(0x1A8), 0x0700); for (int i = 0; i < 7; i++) write16(SPU(0x1A8), 0);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0010));
    for (int i = 0; i < 200000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
    idle(20000);
    write16(SPU(0x1AA), cnt);
    idle(4000);
    for (int v = 0; v < 24; v++) {
        const uint32_t o = (uint32_t)v * 16;
        write16(SPU(o + 0), 0); write16(SPU(o + 2), 0); write16(SPU(o + 4), 0x1000);
        write16(SPU(o + 6), (uint16_t)(0x1800 / 8)); write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
    }
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    write16(SPU(0x188), 0xFFFF); write16(SPU(0x18A), 0x00FF);
    idle(SAMPLE * 16u);
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(SAMPLE * 64u);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    write16(SPU(12 * 16 + 6), (uint16_t)(0x1000 / 8));
    write16(SPU(0x1A4), 0x1000 / 8);
    write16(SPU(0x188), 0x1000); write16(SPU(0x18A), 0);
    idle(SAMPLE * 200u);
    for (int i = 0; i < 4; i++) { run_at((now() + 2000u) & 0xFFFF, sink, cnt, tl_a_s); now(); }
    base = now();

    for (unsigned r = 0; r < NA; r++) readTimes(r);
    static int16_t e[NQ][2];
    for (unsigned r = 0; r < NQ; r++) { e[r][0] = (int16_t)edgeOf(QRUNS[r].tail); e[r][1] = (int16_t)edgeOf(QRUNS[r].tail); }

    write16(SPU(0x1AA), cnt);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay); write32(0x1F801020, comDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("B found 1F801014h %08x 1F801020h %08x 1F801018h %08x 1F801060h %08x; run at 200931e1 / 00031125\n", (unsigned)spuDelay, (unsigned)comDelay, (unsigned)cdDelay, (unsigned)ramSize);
    for (unsigned r = 0; r < NA; r++) {
        printf("A %s :", ARUNS[r].name);
        for (int k = 0; k < 8; k++) printf(" %d/%d", ad[r][k][0], ad[r][k][1]);
        printf("\n");
    }
    for (unsigned r = 0; r < NQ; r++) printf("Q %s : %d %d\n", QRUNS[r].name, e[r][0], e[r][1]);
    printf("\nspu/spu-wqread: Done.\n");
    for (;;) VSync(0);
}
