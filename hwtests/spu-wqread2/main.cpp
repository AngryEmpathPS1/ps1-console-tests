/*
 * spu/spu-wqread2 - THE CYCLES A READ CANNOT BE TAKEN IN, BEHIND EACH KIND OF QUEUE: its arrival swept a cycle at a time (2026-11-25,
 * the SPU session). hwtests/spu-wqread's second part.
 *
 * WHY. hwtests/spu-wqread (ten consoles, identical): behind `S S` a Timer 2 read is taken at once, behind `S S S` three cycles late
 * and the read after it eight; behind mixed RAM / SPU queues neither rule holds as written. One arrival a queue does not show WHICH
 * cycles are closed to the read. This moves the read's arrival through each queue's life with 0-8 nops.
 *
 * WHAT IT DOES. Interrupts off, sound muted, Timer 2 the system clock; hwtests/spu-wqland's run_at starts each run at an exact count.
 *   A queue k : d1/d2 x5     the queue's stores, k nops, then two Timer 2 reads back to back: d1 = the first read's value less
 *                           the run's start, d2 = the second's less the first's (five trials at scattered starts)
 * Queues (S = sh to an SPU voice's volume, W = sw there, R = sh to RAM in one row): none (the reference), S, S S, S S S, S R, S R R,
 * S R S, S S R, R S, R S S, R R S, R S R, W, W W, R. The reader (experiments/spu_wqread2.js) prints, per queue, the cycle each
 * arrival was taken at: an arrival taken later than it came was closed out.
 * FOUR PARTS (spu-wqread2, 2b, 2c, 2d: PART 1-4), each under 4 KB of tails: the first cut held all of them in one program and the
 * tails 4 KB from run_at shared its I-cache lines - a refill inside the timed window, the same on consoles and emulator (`R S`,
 * `S R S` k 5-8, `S S R` k 0, 1, 8 read 10-45 cycles long). Part 4 ends each run with a load from the SPU (P = lhu SPUSTAT) and ONE
 * Timer 2 read: queues P, S P, S S P, R P, W P; d2 is not a time there.
 * No assertions. Every wait is bounded. THE RULE: run here first: `node hwtests/run.js spu-wqread2 --ntsc --frames=3000 --quiet=2000`.
 */
#ifndef PART
#define PART 1
#endif
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static volatile uint16_t sink[8] __attribute__((aligned(16)));

extern "C" { unsigned long long run_at(uint32_t target, volatile uint16_t* ram, uint32_t enable, const void* tail); }
#define S_ "  sh $t6, 0x1c00($t0)\n"
#define S2 "  sh $t6, 0x1c02($t0)\n"
#define S3 "  sh $t6, 0x1c10($t0)\n"
#define W_ "  sw $t6, 0x1c00($t0)\n"
#define W2 "  sw $t6, 0x1c10($t0)\n"
#define R_ "  sh $t6, 0($a1)\n"
#define R2 "  sh $t6, 2($a1)\n"
#define N_ "  nop\n"
#define L_ "  lhu $v1, 0x1120($t0)\n"
#define M_ "  lhu $t7, 0x1120($t0)\n"
#define TAIL(name, body) ".globl tl_" name "\ntl_" name ":\n" body L_ M_ "  nop\n  sll $t7, $t7, 16\n  or $v1, $v1, $t7\n  jr $ra\n  move $v0, $zero\n"
#define P_ "  lhu $t8, 0x1dae($t0)\n"
#define TAILP(name, body) ".globl tl_" name "\ntl_" name ":\n" body P_ L_ "  nop\n  jr $ra\n  move $v0, $zero\n"
#define ROWP(p, b) TAILP(p "_0", b) TAILP(p "_1", b N_) TAILP(p "_2", b N_ N_) TAILP(p "_3", b N_ N_ N_) TAILP(p "_4", b N_ N_ N_ N_) \
    TAILP(p "_5", b N_ N_ N_ N_ N_) TAILP(p "_6", b N_ N_ N_ N_ N_ N_) TAILP(p "_7", b N_ N_ N_ N_ N_ N_ N_) TAILP(p "_8", b N_ N_ N_ N_ N_ N_ N_ N_)
#define ROW(p, b) TAIL(p "_0", b) TAIL(p "_1", b N_) TAIL(p "_2", b N_ N_) TAIL(p "_3", b N_ N_ N_) TAIL(p "_4", b N_ N_ N_ N_) \
    TAIL(p "_5", b N_ N_ N_ N_ N_) TAIL(p "_6", b N_ N_ N_ N_ N_ N_) TAIL(p "_7", b N_ N_ N_ N_ N_ N_ N_) TAIL(p "_8", b N_ N_ N_ N_ N_ N_ N_ N_)
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
#if PART == 1
    ROW("z", "") ROW("s", S_) ROW("ss", S_ S2) ROW("sss", S_ S2 S3) ROW("w", W_)
#elif PART == 2
    ROW("sr", S_ R_) ROW("srr", S_ R_ R2) ROW("srs", S_ R_ S2) ROW("ssr", S_ S2 R_) ROW("ww", W_ W2)
#elif PART == 3
    ROW("rs", R_ S_) ROW("rss", R_ S_ S2) ROW("rrs", R_ R2 S_) ROW("rsr", R_ S_ R2) ROW("r", R_)
#else
    ROWP("p", "") ROWP("sp", S_) ROWP("ssp", S_ S2) ROWP("rp", R_) ROWP("wp", W_)
#endif
    ".set pop\n");
#define X9(p) extern "C" { extern const char tl_##p##_0[], tl_##p##_1[], tl_##p##_2[], tl_##p##_3[], tl_##p##_4[], tl_##p##_5[], tl_##p##_6[], tl_##p##_7[], tl_##p##_8[]; }
#define T9(p) { tl_##p##_0, tl_##p##_1, tl_##p##_2, tl_##p##_3, tl_##p##_4, tl_##p##_5, tl_##p##_6, tl_##p##_7, tl_##p##_8 }
struct Queue { const char* name; const void* tail[9]; };
#if PART == 1
X9(z) X9(s) X9(ss) X9(sss) X9(w)
static const Queue QS[] = { { "-", T9(z) }, { "S", T9(s) }, { "S S", T9(ss) }, { "S S S", T9(sss) }, { "W", T9(w) } };
#define WARM tl_s_0
#elif PART == 2
X9(sr) X9(srr) X9(srs) X9(ssr) X9(ww)
static const Queue QS[] = { { "S R", T9(sr) }, { "S R R", T9(srr) }, { "S R S", T9(srs) }, { "S S R", T9(ssr) }, { "W W", T9(ww) } };
#define WARM tl_sr_0
#elif PART == 3
X9(rs) X9(rss) X9(rrs) X9(rsr) X9(r)
static const Queue QS[] = { { "R S", T9(rs) }, { "R S S", T9(rss) }, { "R R S", T9(rrs) }, { "R S R", T9(rsr) }, { "R", T9(r) } };
#define WARM tl_r_0
#else
X9(p) X9(sp) X9(ssp) X9(rp) X9(wp)
static const Queue QS[] = { { "P", T9(p) }, { "S P", T9(sp) }, { "S S P", T9(ssp) }, { "R P", T9(rp) }, { "W P", T9(wp) } };
#define WARM tl_p_0
#endif
#define NQ (sizeof(QS) / sizeof(QS[0]))

static int16_t ad[NQ][9][5][2];
static void readTimes(unsigned q, int k) {
    const void* tail = QS[q].tail[k];
    for (int n = -2; n < 5; n++) {
        int16_t d1 = -1, d2 = -1;
        for (int tries = 0; tries < 4; tries++) {
            idle(1500u + (uint32_t)((n + 2) * 211 + k * 37));
            const uint32_t target = now() + 2000u;
            while ((int32_t)(target - now()) > 700) {}
            const unsigned long long ret = run_at(target & 0xFFFF, sink, 0, tail);
            now();
            if ((uint32_t)ret != 0) continue;
            const uint32_t v = (uint32_t)(ret >> 32);
            d1 = (int16_t)(((v & 0xFFFF) - (target & 0xFFFF)) & 0xFFFF);
            d2 = (int16_t)(((v >> 16) - (v & 0xFFFF)) & 0xFFFF);
            break;
        }
        if (n >= 0) { ad[q][k][n][0] = d1; ad[q][k][n][1] = d2; }
    }
}

int main() {
    printf("\nspu/spu-wqread2\n");
    printf("The cycles a read cannot be taken in behind each kind of queue: A queue k : d1/d2 x5 (cycles). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint32_t spuDelay = read32(0x1F801014), comDelay = read32(0x1F801020);
    write32(0x1F801014, 0x200931E1); write32(0x1F801020, 0x00031125);
    const uint16_t cnt0 = read16(SPU(0x1AA));
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    for (int i = 0; i < 4; i++) { run_at((now() + 2000u) & 0xFFFF, sink, 0, WARM); now(); }

    for (unsigned q = 0; q < NQ; q++) for (int k = 0; k < 9; k++) readTimes(q, k);

    write16(SPU(0x1AA), cnt0);
    write32(0x1F801014, spuDelay); write32(0x1F801020, comDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("B found 1F801014h %08x 1F801020h %08x; run at 200931e1 / 00031125\n", (unsigned)spuDelay, (unsigned)comDelay);
    for (unsigned q = 0; q < NQ; q++) for (int k = 0; k < 9; k++) {
        printf("A %s %d :", QS[q].name, k);
        for (int n = 0; n < 5; n++) printf(" %d/%d", ad[q][k][n][0], ad[q][k][n][1]);
        printf("\n");
    }
    printf("\nspu/spu-wqread2: Done.\n");
    for (;;) VSync(0);
}
