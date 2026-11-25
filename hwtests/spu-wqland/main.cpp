/*
 * spu/spu-wqland - when an SPU register store queued behind other stores REACHES THE SPU, to the cycle (2026-11-25, the SPU
 * session)
 *
 * WHY. SPU_STORE_TURNS' KNOWN-OPEN (v0.377.375): the console fuzzer's spuwprobe rows time a Timer 2 read behind a run of
 * stores, and where RAM stores are mixed with SPU ones this core is 2-5 cycles long (38 cells of 894). Those rows see the bus
 * through one on-die read's wait, and no rule for the stores' order fits them all (experiments/fuzz-all/spuwsolve.js: the best
 * of 51,840 reads 60 of 66 cells). This looks at the bus from the other end: the cycle the last store of a run lands in the SPU.
 *
 * WHAT IT DOES. Interrupts off, sound muted. Voice 12 loops on one silent block at the IRQ address, so an IRQ enabled at any
 * time rises in that voice's next slot of the sample (SPU_IRQ_SHOWN's armed rule). A trial: the IRQ off and acknowledged; at
 * an exact cycle (counted off Timer 2 into a sled of nops, as hwtests/spu-irqgap) a run of stores whose LAST is the one that
 * turns the IRQ on (SPUCNT bit 6); 450 cycles later I_STAT bit 9 is read. Set = the enable landed ahead of the slot. The run's
 * start is stepped round the 768-cycle sample (16 places) to find where "set" turns to "clear", and that edge bisected: the
 * last start cycle for which the enable still makes the slot. A run that keeps its last store on the bus n cycles longer has
 * its edge n cycles sooner. Each run twice.
 *   Q run : e1 e2      the edge, a cycle of the sample (0-767) on the run's own clock; -1 = no edge found
 * Runs (E = the enable, sh to 1F801DAAh; S = sh to an SPU voice's volume; W = sw there; R = sh to RAM, one row; C = sb to the
 * CD's index register; D = sh to Timer 1's target; n = a nop). E alone is the reference: (E's edge less a run's) less the
 * run's instructions ahead of its E is how much later that E landed for the stores ahead of it.
 * No assertions. Every wait is bounded. THE RULE: run here first.
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

/* run_at(target, ram, enable, tail): a loop reads Timer 2 until under 96 cycles are left to `target` (its 16 bits), jumps into
 * a sled of nops that many long less 40, then into `tail` - the run of stores ($t0 = 1F800000h, $a1 = the RAM halfwords, $a2 =
 * SPUCNT with bit 6 set, $t6 = 0). v0 = 0, or -1 when the target had passed. */
extern "C" { uint32_t run_at(uint32_t target, volatile uint16_t* ram, uint32_t enable, const void* tail); }
#define S_ "  sh $t6, 0x1c00($t0)\n"
#define S2 "  sh $t6, 0x1c02($t0)\n"
#define S3 "  sh $t6, 0x1c10($t0)\n"
#define W_ "  sw $t6, 0x1c00($t0)\n"
#define W2 "  sw $t6, 0x1c10($t0)\n"
#define R_ "  sh $t6, 0($a1)\n"
#define R2 "  sh $t6, 2($a1)\n"
#define R3 "  sh $t6, 4($a1)\n"
#define C_ "  sb $t6, 0x1800($t0)\n"
#define D_ "  sh $t6, 0x1118($t0)\n"
#define N_ "  nop\n"
#define E_ "  sh $a2, 0x1daa($t0)\n"
#define TAIL(name, body) ".globl tl_" name "\ntl_" name ":\n" body "  jr $ra\n  move $v0, $zero\n"
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl run_at\n.type run_at,@function\nrun_at:\n"
    "  lui $t0, 0x1f80\n"
    "  andi $a0, $a0, 0xffff\n"
    "  move $t6, $zero\n"
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
    TAIL("e", E_)
    TAIL("se", S_ E_) TAIL("sse", S_ S2 E_) TAIL("ssse", S_ S2 S3 E_)
    TAIL("re", R_ E_) TAIL("rre", R_ R2 E_) TAIL("rrre", R_ R2 R3 E_)
    TAIL("rse", R_ S_ E_) TAIL("sre", S_ R_ E_) TAIL("rrse", R_ R2 S_ E_) TAIL("srre", S_ R_ R2 E_) TAIL("ssre", S_ S2 R_ E_)
    TAIL("srse", S_ R_ S2 E_) TAIL("rsre", R_ S_ R2 E_)
    TAIL("rn1e", R_ N_ E_) TAIL("rn2e", R_ N_ N_ E_) TAIL("rn3e", R_ N_ N_ N_ E_) TAIL("rn4e", R_ N_ N_ N_ N_ E_)
    TAIL("rn5e", R_ N_ N_ N_ N_ N_ E_) TAIL("rn6e", R_ N_ N_ N_ N_ N_ N_ E_) TAIL("rn7e", R_ N_ N_ N_ N_ N_ N_ N_ E_)
    TAIL("rrn2e", R_ R2 N_ N_ E_) TAIL("rrn4e", R_ R2 N_ N_ N_ N_ E_)
    TAIL("sn2e", S_ N_ N_ E_) TAIL("sn4e", S_ N_ N_ N_ N_ E_) TAIL("sn6e", S_ N_ N_ N_ N_ N_ N_ E_) TAIL("sn8e", S_ N_ N_ N_ N_ N_ N_ N_ N_ E_)
    TAIL("we", W_ E_) TAIL("wwe", W_ W2 E_) TAIL("ce", C_ E_) TAIL("de", D_ E_) TAIL("sde", S_ D_ E_)
    ".set pop\n");
#define X(n) extern "C" { extern const char tl_##n[]; }
X(e) X(se) X(sse) X(ssse) X(re) X(rre) X(rrre) X(rse) X(sre) X(rrse) X(srre) X(ssre) X(srse) X(rsre)
X(rn1e) X(rn2e) X(rn3e) X(rn4e) X(rn5e) X(rn6e) X(rn7e) X(rrn2e) X(rrn4e) X(sn2e) X(sn4e) X(sn6e) X(sn8e) X(we) X(wwe) X(ce) X(de) X(sde)
struct Run { const char* name; const void* tail; };
static const Run RUNS[] = {
    { "E", tl_e }, { "S E", tl_se }, { "S S E", tl_sse }, { "S S S E", tl_ssse },
    { "R E", tl_re }, { "R R E", tl_rre }, { "R R R E", tl_rrre },
    { "R S E", tl_rse }, { "S R E", tl_sre }, { "R R S E", tl_rrse }, { "S R R E", tl_srre }, { "S S R E", tl_ssre }, { "S R S E", tl_srse }, { "R S R E", tl_rsre },
    { "R n1 E", tl_rn1e }, { "R n2 E", tl_rn2e }, { "R n3 E", tl_rn3e }, { "R n4 E", tl_rn4e }, { "R n5 E", tl_rn5e }, { "R n6 E", tl_rn6e }, { "R n7 E", tl_rn7e },
    { "R R n2 E", tl_rrn2e }, { "R R n4 E", tl_rrn4e },
    { "S n2 E", tl_sn2e }, { "S n4 E", tl_sn4e }, { "S n6 E", tl_sn6e }, { "S n8 E", tl_sn8e },
    { "W E", tl_we }, { "W W E", tl_wwe }, { "C E", tl_ce }, { "D E", tl_de }, { "S D E", tl_sde },
    { "E", tl_e },
};

static uint32_t base;
/* One trial: the run started at cycle phi of a sample (on this program's clock). 1 = I_STAT bit 9 set 450 cycles on, 0 = clear. */
static int trial(const void* tail, uint32_t phi) {
    for (int tries = 0; tries < 4; tries++) {
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
        idle(SAMPLE * 2u);
        write32(I_STAT, ~0x200u);
        const uint32_t n = (now() - base) / SAMPLE + 3, target = base + n * SAMPLE + phi;
        while ((int32_t)(target - now()) > 700) {}
        const uint32_t r = run_at(target & 0xFFFF, sink, (uint32_t)(cnt | 0x0040), tail);
        while ((int32_t)(target + 450u - now()) > 0) {}
        const uint32_t i = read32(I_STAT);
        now();
        if (r == 0) return (i & 0x200u) ? 1 : 0;
    }
    return -1;
}
static char scan[80][17]; static int scanN;   /* each edge's 16 places as read: 1 set, 0 clear, - the run came late */
/* the last start cycle that still makes the slot: found on 16 places round the sample, then bisected */
static int edgeOf(const void* tail) {
    for (int w = 0; w < 3; w++) trial(tail, 0);                 /* the run's code into the cache */
    int o[16];
    for (int i = 0; i < 16; i++) o[i] = trial(tail, (uint32_t)i * 48u);
    if (scanN < 80) { for (int i = 0; i < 16; i++) scan[scanN][i] = o[i] == 1 ? '1' : o[i] == 0 ? '0' : '-'; scan[scanN][16] = 0; scanN++; }
    for (int i = 0; i < 16; i++) {
        if (o[i] != 1 || o[(i + 1) & 15] != 0) continue;
        uint32_t lo = (uint32_t)i * 48u, hi = lo + 48u;         /* set at lo, clear at hi (hi may be 768 = 0) */
        while (hi - lo > 1) { const uint32_t mid = lo + (hi - lo) / 2; const int r = trial(tail, mid % SAMPLE); if (r == 1) lo = mid; else hi = mid; }
        return (int)(lo % SAMPLE);
    }
    return -1;
}

int main() {
    printf("\nspu/spu-wqland\n");
    printf("When an SPU store queued behind other stores reaches the SPU: Q run : edge edge (the last start cycle that still makes the slot). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    /* the bus registers as found (printed: a program run before this one may have left its own), then the kernel's */
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
    /* one silent block at 1000h that loops on itself, written by hand (manual write): start + end + repeat */
    write16(SPU(0x1A6), 0x1000 / 8);
    write16(SPU(0x1A8), 0x0700); for (int i = 0; i < 7; i++) write16(SPU(0x1A8), 0);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0010));
    for (int i = 0; i < 200000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
    idle(20000);
    write16(SPU(0x1AA), cnt);
    idle(4000);
    /* the same at 1800h: every voice is keyed into it and off again, so none wanders into the IRQ's block (a silent voice goes
     * on reading its blocks, and one that reaches a looping block stays in it) */
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
    write16(SPU(0x1A4), 0x1000 / 8);                            /* the IRQ address: the block */
    write16(SPU(0x188), 0x1000); write16(SPU(0x18A), 0);        /* voice 12 on, alone */
    idle(SAMPLE * 200u);
    for (int i = 0; i < 4; i++) { run_at((now() + 2000u) & 0xFFFF, sink, cnt, tl_se); now(); }
    base = now();

    static int16_t e[sizeof(RUNS) / sizeof(RUNS[0])][2];
    for (unsigned r = 0; r < sizeof(RUNS) / sizeof(RUNS[0]); r++) { e[r][0] = (int16_t)edgeOf(RUNS[r].tail); e[r][1] = (int16_t)edgeOf(RUNS[r].tail); }

    write16(SPU(0x1AA), cnt);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay); write32(0x1F801020, comDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("B found 1F801014h %08x 1F801020h %08x 1F801018h %08x 1F801060h %08x; run at 200931e1 / 00031125\n", (unsigned)spuDelay, (unsigned)comDelay, (unsigned)cdDelay, (unsigned)ramSize);
    for (unsigned r = 0; r < sizeof(RUNS) / sizeof(RUNS[0]); r++) printf("Q %s : %d %d   %s %s\n", RUNS[r].name, e[r][0], e[r][1], scan[2 * r], scan[2 * r + 1]);
    printf("\nspu/spu-wqland: Done.\n");
    for (;;) VSync(0);
}
