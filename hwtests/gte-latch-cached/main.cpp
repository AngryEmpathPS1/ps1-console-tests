/*
 * gte/gte-latch-cached - the GTE's input latches in CYCLES, from a warm cache.
 *
 * pcsx-redux nugget's gte-latency (RESULTS.md, SCPH-5501) gives, per (command,
 * input register), the smallest N such that an MTC2/CTC2 N nops after the
 * command no longer changes its result. Its probes are ~13 KB of unrolled code
 * against a 4 KB instruction cache, so their nops run with a line fill every
 * four: an N there is ~2N+1 cycles, with a 4-cycle step at each line (STATUS
 * M233). So each of its N pins a latch only to a window of cycles, 1.6 wide on
 * average, and M233 put this core's latch (GTE_LATCH) at each window's middle.
 * Games run such code from a warm cache, one instruction a cycle - and where a
 * game writes inside a window, the window's width is the whole answer.
 * Mortal Kombat 4's `NCT; nop; mtc2 VXY0; mtc2 VZ0` puts VZ0 at +3 cycles
 * against a window of [2 or 6, 6] (M262).
 *
 * This runs the same scenes, commands (sf=1 lm=1) and canaries as the nugget
 * probes, but each (cell, N) is one function of about 1 KB - scene, two nops,
 * the command, N nops, the canary write, 48 nops (past every command, and past
 * any hold, M262), the outputs read - whose body runs twice in a loop, the
 * second pass from the cache the first filled. (A first version warmed it with
 * a separate call, and the caller's own lines evicted some of the probe's in
 * between: NCCT RGBC read UNSTABLE here.) From a warm cache the write lands
 * N + 1 cycles after the command, so minN IS the latch in cycles: the first N
 * whose outputs equal the baseline (the same function with the write 70 nops
 * on). The row shows every N: C = changed, . = baseline. UNSTABLE if two calls
 * disagree.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.262 (2026-09-28, `node hwtests/run.js gte-latch-cached`),
 * minN is GTE_LATCH's cycle in every cell, by construction, and every row stable:
 *     RTPS  H 3, OFX 3, DQA 8, DQB 7           RTPT VXY1 7, VXY2 2, R13R21 10,
 *     NCT   VZ0 4, VZ1 5, VXY2 3, VZ2 7             R22R23 8, TRY 8, H 11, DQA 15
 *     NCCT  RGBC 26    NCDT RGBC 31, RFC 25   DCPL IR1 3   INTPL IR0 3
 *     DPCT  RGB0 0, RGB1 0 (Redux 4: the FIFO push timing, STATUS M233), IR0 10
 *     DPCS  IR0 3 (added 2026-09-29: Blood Lines (Europe) (Beta) writes DPCS's IR0 +3 cycles after
 *           issue, 171,611 times in the European deep pass - exactly on this latch; Redux N 1)
 *     controls RTPS VZ0, NCT VXY0, DPCT RGB2: 0
 * A core that latches every input at issue (v0.261 with GTE_LATCH off) reads 0
 * in every cell. What a console reads is each latch to the cycle: NCT VZ0 < 3
 * would say Mortal Kombat 4's write at +3 is not read (M262's knife-edge).
 */
#include <common.h>
#include <psxapi.h>

#define MAXN 32
#define NOUT 19

#define CTC2(R, V) __asm__ volatile("ctc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))
#define MTC2(R, V) __asm__ volatile("mtc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))
#define MFC2(R, D) __asm__ volatile("mfc2 %0, $" #R "\n\tnop\n\tnop" : "=r"(D))
#define CFC2(R, D) __asm__ volatile("cfc2 %0, $" #R "\n\tnop\n\tnop" : "=r"(D))
#define ALWAYS static inline __attribute__((always_inline))

/* nugget gte-latency-perspective.c scene_setup */
ALWAYS void sceneP() {
    CTC2(0, 0x1000); CTC2(1, 0); CTC2(2, 0x1000); CTC2(3, 0); CTC2(4, 0x1000);
    CTC2(5, 100); CTC2(6, 200); CTC2(7, 0x4000);
    CTC2(24, 160 << 16); CTC2(25, 120 << 16); CTC2(26, 200); CTC2(27, 0x100); CTC2(28, 0);
    MTC2(0, (0x0400u << 16) | 0x0300u); MTC2(1, 0x500);
    MTC2(2, (0x0700u << 16) | 0x0600u); MTC2(3, 0x800);
    MTC2(4, (0x0A00u << 16) | 0x0900u); MTC2(5, 0xB00);
    MTC2(12, 0); MTC2(13, 0); MTC2(14, 0);
    MTC2(16, 0); MTC2(17, 0); MTC2(18, 0); MTC2(19, 0);
    MTC2(20, 0); MTC2(21, 0); MTC2(22, 0);
    MTC2(24, 0); MTC2(25, 0); MTC2(26, 0); MTC2(27, 0); MTC2(8, 0);
    CTC2(31, 0);
}
/* nugget gte-latency.c scene_setup (lighting) */
ALWAYS void sceneL() {
    CTC2(8, 0x1000); CTC2(9, 0); CTC2(10, 0x1000); CTC2(11, 0); CTC2(12, 0x1000);
    CTC2(16, 0x1000); CTC2(17, 0); CTC2(18, 0x1000); CTC2(19, 0); CTC2(20, 0x1000);
    CTC2(13, 0); CTC2(14, 0); CTC2(15, 0);
    CTC2(21, 0); CTC2(22, 0); CTC2(23, 0);
    MTC2(0, 0); MTC2(1, 0x1000);
    MTC2(2, 0x1000); MTC2(3, 0);
    MTC2(4, 0x1000u << 16); MTC2(5, 0);
    MTC2(6, 0x00808080); MTC2(8, 0x1000);
    CTC2(31, 0);
}
/* ...and scene_setup_ncdt */
ALWAYS void sceneLD() {
    sceneL();
    CTC2(21, 0x1000); CTC2(22, 0x1000); CTC2(23, 0x1000);
    MTC2(8, 0x0800);
}
/* nugget gte-latency-dpc.c scene_setup */
ALWAYS void sceneD() {
    MTC2(6, 0x00808080); MTC2(8, 0x0800); MTC2(9, 0x0500); MTC2(10, 0x0600); MTC2(11, 0x0700);
    MTC2(20, 0x00808080); MTC2(21, 0x00404040); MTC2(22, 0x00202020);
    CTC2(21, 0x1000); CTC2(22, 0x1000); CTC2(23, 0x1000);
    MTC2(12, 0); MTC2(13, 0); MTC2(14, 0);
    MTC2(16, 0); MTC2(17, 0); MTC2(18, 0); MTC2(19, 0);
    MTC2(24, 0); MTC2(25, 0); MTC2(26, 0); MTC2(27, 0);
    CTC2(31, 0);
}
/* the state nugget's probe_result_t compares */
ALWAYS void readAll(uint32_t* o) {
    MFC2(20, o[0]); MFC2(21, o[1]); MFC2(22, o[2]);
    MFC2(24, o[3]); MFC2(25, o[4]); MFC2(26, o[5]); MFC2(27, o[6]);
    MFC2(8, o[7]); MFC2(9, o[8]); MFC2(10, o[9]); MFC2(11, o[10]);
    MFC2(12, o[11]); MFC2(13, o[12]); MFC2(14, o[13]);
    MFC2(16, o[14]); MFC2(17, o[15]); MFC2(18, o[16]); MFC2(19, o[17]);
    CFC2(31, o[18]);
}

/* cop2 words, sf=1 lm=1 with Sony's instruction number (nugget cop2.h COP2_OP) */
#define C_RTPS  "0x4A180401"
#define C_RTPT  "0x4A280430"
#define C_NCT   "0x4AD80420"
#define C_NCCT  "0x4B18043F"
#define C_NCDT  "0x4AF80416"
#define C_DPCT  "0x4AF8042A"
#define C_DPCS  "0x4A780410"
#define C_DCPL  "0x4A680429"
#define C_INTPL "0x4A980411"

typedef void (*probe_fn)(uint32_t, uint32_t*);

/* The sequence runs twice INSIDE the function, a loop the compiler cannot unroll (the trip count is read from
 * memory), and the second pass is the measurement: nothing but the loop's own branch runs between the pass
 * that fills the cache and the one that runs from it. */
static volatile int passes = 2;
#define PROBE(NAME, SCENE, CMD, WOP, REG, N)                                \
    extern "C" void __attribute__((noinline)) NAME##_##N(uint32_t canary, uint32_t* o) { \
        for (int pass = 0, np = passes; pass < np; pass++) {                \
            SCENE();                                                        \
            __asm__ volatile(                                               \
                ".set push\n\t.set noreorder\n\t"                           \
                "nop\n\tnop\n\t"                                            \
                ".word " CMD "\n\t"                                         \
                ".rept " #N "\n\tnop\n\t.endr\n\t"                          \
                WOP " %0, $" #REG "\n\t"                                    \
                ".rept 48\n\tnop\n\t.endr\n\t"                              \
                ".set pop\n\t"                                              \
                : : "r"(canary));                                           \
            readAll(o);                                                     \
        }                                                                   \
    }
#define CELL(NAME, SCENE, CMD, WOP, REG) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 0)  PROBE(NAME, SCENE, CMD, WOP, REG, 1)  PROBE(NAME, SCENE, CMD, WOP, REG, 2)  \
    PROBE(NAME, SCENE, CMD, WOP, REG, 3)  PROBE(NAME, SCENE, CMD, WOP, REG, 4)  PROBE(NAME, SCENE, CMD, WOP, REG, 5)  \
    PROBE(NAME, SCENE, CMD, WOP, REG, 6)  PROBE(NAME, SCENE, CMD, WOP, REG, 7)  PROBE(NAME, SCENE, CMD, WOP, REG, 8)  \
    PROBE(NAME, SCENE, CMD, WOP, REG, 9)  PROBE(NAME, SCENE, CMD, WOP, REG, 10) PROBE(NAME, SCENE, CMD, WOP, REG, 11) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 12) PROBE(NAME, SCENE, CMD, WOP, REG, 13) PROBE(NAME, SCENE, CMD, WOP, REG, 14) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 15) PROBE(NAME, SCENE, CMD, WOP, REG, 16) PROBE(NAME, SCENE, CMD, WOP, REG, 17) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 18) PROBE(NAME, SCENE, CMD, WOP, REG, 19) PROBE(NAME, SCENE, CMD, WOP, REG, 20) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 21) PROBE(NAME, SCENE, CMD, WOP, REG, 22) PROBE(NAME, SCENE, CMD, WOP, REG, 23) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 24) PROBE(NAME, SCENE, CMD, WOP, REG, 25) PROBE(NAME, SCENE, CMD, WOP, REG, 26) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 27) PROBE(NAME, SCENE, CMD, WOP, REG, 28) PROBE(NAME, SCENE, CMD, WOP, REG, 29) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 30) PROBE(NAME, SCENE, CMD, WOP, REG, 31) PROBE(NAME, SCENE, CMD, WOP, REG, 32) \
    PROBE(NAME, SCENE, CMD, WOP, REG, 70)
#define TAB(NAME) { NAME##_0, NAME##_1, NAME##_2, NAME##_3, NAME##_4, NAME##_5, NAME##_6, NAME##_7, NAME##_8, \
    NAME##_9, NAME##_10, NAME##_11, NAME##_12, NAME##_13, NAME##_14, NAME##_15, NAME##_16, NAME##_17, NAME##_18, \
    NAME##_19, NAME##_20, NAME##_21, NAME##_22, NAME##_23, NAME##_24, NAME##_25, NAME##_26, NAME##_27, NAME##_28, \
    NAME##_29, NAME##_30, NAME##_31, NAME##_32 }, NAME##_70

/* The cells: the ones games were found writing inside (M262's four discs), the longest windows, the open
 * DPCT FIFO cells, and two controls the console reads at issue (N = 0). */
CELL(rtps_vz0,    sceneP,  C_RTPS,  "mtc2", 1)
CELL(rtps_h,      sceneP,  C_RTPS,  "ctc2", 26)
CELL(rtps_ofx,    sceneP,  C_RTPS,  "ctc2", 24)
CELL(rtps_dqa,    sceneP,  C_RTPS,  "ctc2", 27)
CELL(rtps_dqb,    sceneP,  C_RTPS,  "ctc2", 28)
CELL(rtpt_vxy1,   sceneP,  C_RTPT,  "mtc2", 2)
CELL(rtpt_vxy2,   sceneP,  C_RTPT,  "mtc2", 4)
CELL(rtpt_r13r21, sceneP,  C_RTPT,  "ctc2", 1)
CELL(rtpt_r22r23, sceneP,  C_RTPT,  "ctc2", 2)
CELL(rtpt_try,    sceneP,  C_RTPT,  "ctc2", 6)
CELL(rtpt_h,      sceneP,  C_RTPT,  "ctc2", 26)
CELL(rtpt_dqa,    sceneP,  C_RTPT,  "ctc2", 27)
CELL(nct_vxy0,    sceneL,  C_NCT,   "mtc2", 0)
CELL(nct_vz0,     sceneL,  C_NCT,   "mtc2", 1)
CELL(nct_vz1,     sceneL,  C_NCT,   "mtc2", 3)
CELL(nct_vxy2,    sceneL,  C_NCT,   "mtc2", 4)
CELL(nct_vz2,     sceneL,  C_NCT,   "mtc2", 5)
CELL(ncct_rgbc,   sceneL,  C_NCCT,  "mtc2", 6)
CELL(ncdt_rgbc,   sceneLD, C_NCDT,  "mtc2", 6)
CELL(ncdt_rfc,    sceneLD, C_NCDT,  "ctc2", 21)
CELL(dcpl_ir1,    sceneD,  C_DCPL,  "mtc2", 9)
CELL(dpct_rgb0,   sceneD,  C_DPCT,  "mtc2", 20)
CELL(dpct_rgb1,   sceneD,  C_DPCT,  "mtc2", 21)
CELL(dpct_rgb2,   sceneD,  C_DPCT,  "mtc2", 22)
CELL(dpct_ir0,    sceneD,  C_DPCT,  "mtc2", 8)
CELL(dpcs_ir0,    sceneD,  C_DPCS,  "mtc2", 8)
CELL(intpl_ir0,   sceneD,  C_INTPL, "mtc2", 8)

struct Cell { const char* name; probe_fn fn[MAXN + 1]; probe_fn base; uint32_t canary; int redux; };
static const Cell CELLS[] = {
    { "RTPS VZ0 (ctl)", TAB(rtps_vz0),    0x00000400u, 0 },
    { "RTPS H",         TAB(rtps_h),      0x00000080u, 1 },
    { "RTPS OFX",       TAB(rtps_ofx),    0x00800000u, 1 },
    { "RTPS DQA",       TAB(rtps_dqa),    0x00000040u, 4 },
    { "RTPS DQB",       TAB(rtps_dqb),    0x00000080u, 3 },
    { "RTPT VXY1",      TAB(rtpt_vxy1),   0x05000400u, 3 },
    { "RTPT VXY2",      TAB(rtpt_vxy2),   0x05000400u, 2 },
    { "RTPT R13R21",    TAB(rtpt_r13r21), 0x07ff07ffu, 4 },
    { "RTPT R22R23",    TAB(rtpt_r22r23), 0x07ff07ffu, 4 },
    { "RTPT TRY",       TAB(rtpt_try),    0x00007fffu, 4 },
    { "RTPT H",         TAB(rtpt_h),      0x00000080u, 5 },
    { "RTPT DQA",       TAB(rtpt_dqa),    0x00000040u, 7 },
    { "NCT VXY0 (ctl)", TAB(nct_vxy0),    0x40004000u, 0 },
    { "NCT VZ0",        TAB(nct_vz0),     0x00000100u, 2 },
    { "NCT VZ1",        TAB(nct_vz1),     0x00000100u, 1 },
    { "NCT VXY2",       TAB(nct_vxy2),    0x40004000u, 1 },
    { "NCT VZ2",        TAB(nct_vz2),     0x00000100u, 3 },
    { "NCCT RGBC",      TAB(ncct_rgbc),   0x00404040u, 12 },
    { "NCDT RGBC",      TAB(ncdt_rgbc),   0x00404040u, 15 },
    { "NCDT RFC",       TAB(ncdt_rfc),    0x00010000u, 13 },
    { "DCPL IR1",       TAB(dcpl_ir1),    0x00000123u, 1 },
    { "DPCT RGB0",      TAB(dpct_rgb0),   0x00f0f0f0u, 4 },
    { "DPCT RGB1",      TAB(dpct_rgb1),   0x00f0f0f0u, 4 },
    { "DPCT RGB2",      TAB(dpct_rgb2),   0x00f0f0f0u, 0 },
    { "DPCT IR0",       TAB(dpct_ir0),    0x00000400u, 4 },
    { "DPCS IR0",       TAB(dpcs_ir0),    0x00000400u, 1 },
    { "INTPL IR0",      TAB(intpl_ir0),   0x00000400u, 1 },
};

static void gteEnable() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

static int same(const uint32_t* a, const uint32_t* b) {
    for (int i = 0; i < NOUT; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* each call is already warm (PROBE's second pass); two calls must agree */
static int measure(probe_fn f, uint32_t canary, uint32_t* out) {
    uint32_t a[NOUT], b[NOUT];
    f(canary, a);
    f(canary, b);
    for (int i = 0; i < NOUT; i++) out[i] = b[i];
    return same(a, b);
}

int main() {
    initVideo(320, 240);
    printf("\ngte/gte-latch-cached\n");
    printf("cop2; N nops; mtc2/ctc2 canary; 48 nops - from a warm cache, so minN is the latch in cycles.\n");
    printf("C = changed, . = baseline, for N = 0..%d. Redux N is RESULTS.md's, on its thrashing probes.\n", MAXN);
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    gteEnable();
    for (unsigned c = 0; c < sizeof(CELLS) / sizeof(CELLS[0]); c++) {
        const Cell& k = CELLS[c];
        uint32_t base[NOUT], got[NOUT];
        int stable = measure(k.base, k.canary, base);
        char row[MAXN + 2];
        int minN = -1;
        for (int n = 0; n <= MAXN; n++) {
            stable &= measure(k.fn[n], k.canary, got);
            int eq = same(got, base);
            row[n] = eq ? '.' : 'C';
            if (minN < 0 && eq) minN = n;
        }
        row[MAXN + 1] = 0;
        printf("%-15s minN=%2d  Redux N=%2d  %s%s\n", k.name, minN, k.redux, row, stable ? "" : " UNSTABLE");
    }
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) VSync(0);
}
