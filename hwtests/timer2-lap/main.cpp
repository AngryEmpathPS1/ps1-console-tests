/*
 * timers/timer2-lap - how long is a lap of Timer 2 at sysclock/8 with reset at target, and does watching it change that?
 *
 * WHY (2026-10-30). psx-spx dev (commit 814e546, its Timers page, no measurement named) says the lap at /8 is exactly 8T: the
 * system clock's T+2 does not apply there and nothing disturbs the prescaler. STATUS M104 left /8 at T+1 counts, unmeasured.
 * DuckStation's `counter %= target` is 8T. This core's reset is lazy and gives both answers: a read within 8 cycles of the
 * tick that lands on T arms zeroAt and that lap is 8T; anything that catches several ticks up at once (a program that does
 * not poll, schedule()'s event for an IRQ) runs T+1 counts, 8T+8. A console has one counter, so at most one of those is right
 * (experiments/psxspx-compare.md: FF7's sound tick, 138,888 cycles or 138,896).
 *
 * WHAT IT DOES. Interrupts off, I_MASK 0. Timer 1 on the system clock is the ruler, its wraps counted in software; Timer 0 on
 * the system clock reads beside it at each sample as a second ruler. A row starts Timer 2 (target, then MODE), takes a sample
 * (the counter, then the rulers), watches for about K laps, takes another. E = the ruler's difference, dc = the counter's,
 * tick = 8 cycles (1 in the controls):
 *     K = round((E - tick x dc) / nominal lap)        lap = (E - tick x dc) / K
 * which needs no value near T to be read. It can only be fooled by a lap wrong by half a lap in K, so nominal is 8T+4, midway
 * between the two answers, and K stays under T. Each row is several trials with different waits, so a sample falls at every
 * place in a tick and the eight-cycle grain of dc averages out; `spread` is the range of (E - tick x dc - K x 8T) over the
 * trials. All that differs between the rows is what the CPU does between the two samples:
 *   IDLE          only the ruler is read. Timer 2 is left alone for the whole of K laps.
 *   POLL tight    consecutive reads of Timer 2's counter (.rept lhu), every tick seen.
 *   POLL w        reads with a counted wait between them, w = 0, 10, 100. `read every` is the spacing measured.
 *   FLAG I_STAT   no sample at all: Timer 2 raises its IRQ at the target (repeat, pulse), I_STAT.6 is polled and acknowledged,
 *                 lap = the ruler over K edges. Timer 2's registers are not read in those K laps.
 *   FLAG MODE     the same off MODE bit 11, which a read clears.
 * at T = 8000h, 1000h, 100h and 10h.
 *   CONTROLS: the same arithmetic on laps that are known: Timer 2 on the system clock, reset at target (T+2: ps1-tests
 *   timers, M104), and both clocks wrapping at FFFFh (10000h counts: psx-spx). A row here that does not read its known lap
 *   means the instrument is wrong, and nothing else in the test can be read.
 *   DWELL: at T = 10h, reads at a steady spacing (two spacings a cycle apart, so one is odd) into the scratchpad, 131,072 of
 *   them, and a histogram: how long each value lasts. An 8T lap has room for T eight-cycle values among T+1, so T and 0 must
 *   share one or one must never be seen. M105's external clock reads T for 6 cycles, then 0.
 * Every wait is bounded in iterations, whatever the rulers do. The rulers' worst disagreement prints at the end; a refresh
 * between two reads of one sample moves it, 10 cycles at most on this core, and a row flags past RULERS_NOISE.
 *
 * READ IT LIKE THIS. The question is two lines: is `lap` the same in every row of one T (does polling change the lap), and is
 * it 8T, 8T+8 or something else? A lap of 8T+r for r not 0 or 8 would mean the reset moves the prescaler; 8T or 8T+8 exactly
 * means it keeps its grid. The grain is about +-0.25 at T = 10h and less above it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE (hwtests/README.md): run here first. ON THIS CORE AT v0.377.223 (`PSX_MODEL=SCPH-7501 node hwtests/run.js timer2-lap
 * --raw`, about ten seconds of machine time): the controls read their laps, T+1.95..2.06, wraps within 0.6. Then every
 * answer between the two, over 8T, with a read every 5.1 (tight), 9.0, 29 and 209 cycles:
 *     T       IDLE   tight   w=0    w=10   w=100  I_STAT  MODE
 *     8000h  +8.14   +0.13  +1.12  +5.01  +7.18  +8.71   +7.92
 *     1000h  +8.03   +0.03  +1.66  +6.13  +7.59  +8.00   +5.98
 *     0100h  +8.00   -0.05  +2.98  +6.71  +7.37  +8.00   +7.91
 *     0010h  +8.11   +0.67  +1.95  +5.45  +7.81  +8.08   +4.20
 * The w and MODE rows are how often a read lands on the tick of T, so they move with the code's own timing from build to
 * build. T = 10h tight is not 0 because a lap that short meets the gap around the samples. DWELL: value T 7.3-7.6 cycles,
 * value 0 1.7-2.6, and the rest uneven. Under the runner on the virtual console the same picture, other draws in those rows.
 * TESTED ON TWO SCRATCH CORES WITH ONE LAP. With Timer 2 at /8 given span = T: all 28 rows 8T within 0.25, DWELL value 0 8.00
 * cycles, value T 0.00. With no zeroAt for Timer 2: all 28 rows 8T+8 within 0.41, DWELL 7.98-8.00 and 7.99. So the spread above
 * is this core's and not the instrument's, and a console with one lap will read one.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define REG16(a) (*(volatile uint16_t*)(a))
#define I_STAT   (*(volatile uint32_t*)0x1F801070u)
#define I_MASK   (*(volatile uint32_t*)0x1F801074u)
#define T0_COUNT REG16(0x1F801100u)
#define T0_MODE  REG16(0x1F801104u)
#define T1_COUNT REG16(0x1F801110u)
#define T1_MODE  REG16(0x1F801114u)
#define T2_COUNT REG16(0x1F801120u)
#define T2_MODE  REG16(0x1F801124u)
#define T2_TGT   REG16(0x1F801128u)

#define M_RESET  0x0008u     /* reset at target */
#define M_IRQT   0x0010u     /* IRQ at target */
#define M_REPEAT 0x0040u
#define M_DIV8   0x0200u     /* sysclock/8 */
#define I_TMR2   0x0040u
#define F_TARGET 0x0800u     /* MODE: reached target, cleared by the read */

#define CHUNK 20000          /* nominal cycles of Timer 2 polling between two reads of the ruler; its wrap is 65,536 */
#define RULERS_NOISE 24      /* a refresh between two reads of one sample moves their gap, 10 at most on the core */
#define SCRATCH 0x1F800000u
#define BURST 512            /* halfwords, the scratchpad's 1 KB */

static uint32_t hi;
static uint16_t lo;
/* The ruler: Timer 1 and its wraps. */
static inline uint32_t now() {
    const uint16_t t = T1_COUNT;
    if (t < lo) hi += 0x10000;
    lo = t;
    return hi + t;
}

struct Sample { uint32_t ruler; uint16_t count, ruler2; };
/* Not inline: the same code at both ends, warmed by a call before the first, so the gaps between its reads are the same at
 * both ends. A first version inlined it and read the system clock's exact rows up to 8 cycles out (a miss in the first). */
static void __attribute__((noinline)) take(Sample& s) {
    s.count = T2_COUNT;
    s.ruler2 = T0_COUNT;
    s.ruler = now();
}

static void start(uint32_t mode, uint32_t target) {
    T2_TGT = target;
    T2_MODE = mode;
    for (int i = 0; i < 8; i++) now();   /* past the write's hold */
}

/* `passes` x 16 back to back reads of the counter. A call stays under CHUNK cycles. */
static void __attribute__((noinline)) pollTight(uint32_t passes) {
    uint32_t t;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "1:\n\t"
        ".rept 15\n\tlhu %1, 0(%2)\n\t.endr\n\t"
        "addiu %0, %0, -1\n\t"
        "bnez %0, 1b\n\t"
        "lhu %1, 0(%2)\n\t"
        ".set pop\n\t"
        : "+r"(passes), "=&r"(t) : "r"(0x1F801120u));
}

/* `reads` reads of the counter with a counted wait after each. */
static void __attribute__((noinline)) pollSpaced(uint32_t reads, uint32_t wait) {
    uint32_t t, w;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "1:\n\t"
        "lhu %1, 0(%3)\n\t"
        "move %2, %4\n\t"
        "2:\n\t"
        "bnez %2, 2b\n\t"
        "addiu %2, %2, -1\n\t"
        "addiu %0, %0, -1\n\t"
        "bnez %0, 1b\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : "+r"(reads), "=&r"(t), "=&r"(w) : "r"(0x1F801120u), "r"(wait));
}

/* A steady spacing, into the scratchpad. B is A with one more instruction: one of the two spacings is odd. */
static void __attribute__((noinline)) burstA(uint32_t p, uint32_t end) {
    uint32_t t;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "1:\n\t"
        "lhu %1, 0(%2)\n\t"
        "addiu %0, %0, 2\n\t"
        "sh %1, -2(%0)\n\t"
        "bne %0, %3, 1b\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : "+r"(p), "=&r"(t) : "r"(0x1F801120u), "r"(end));
}
static void __attribute__((noinline)) burstB(uint32_t p, uint32_t end) {
    uint32_t t;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "1:\n\t"
        "lhu %1, 0(%2)\n\t"
        "addiu %0, %0, 2\n\t"
        "sh %1, -2(%0)\n\t"
        "nop\n\t"
        "bne %0, %3, 1b\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : "+r"(p), "=&r"(t) : "r"(0x1F801120u), "r"(end));
}

#define IDLE  0xFFFFFFFEu
#define TIGHT 0xFFFFFFFFu

/* Spend about `span` cycles as `how` says; the count of Timer 2 reads comes back. Bounded: every loop here is counted. */
static uint32_t watch(uint32_t how, uint32_t span) {
    if (how == IDLE) {
        const uint32_t t0 = now();
        for (uint32_t n = span; n; n--) if (now() - t0 >= span) break;
        return 0;
    }
    const uint32_t each = how == TIGHT ? 7 : 12 + 2 * how;   /* nominal: the row prints the real spacing */
    const uint32_t chunk = CHUNK / each;
    uint32_t left = span / each, reads = 0;
    if (!left) left = 1;
    while (left) {
        uint32_t n = left < chunk ? left : chunk;
        if (how == TIGHT) {
            n = (n + 15) & ~15u;
            pollTight(n >> 4);
        } else pollSpaced(n, how);
        reads += n;
        left = left > n ? left - n : 0;
        now();
    }
    return reads;
}

static void printFixed(uint32_t whole, uint32_t part, uint32_t of) {
    printf("%u.%02u", whole, part * 100 / of);
}

/* lap = net / K, printed with what it is over `base` */
static void printLap(uint32_t net, uint32_t K, uint32_t base, const char* baseName) {
    printf("lap ");
    printFixed(net / K, net % K, K);
    const int32_t ex = (int32_t)net - (int32_t)(K * base);
    const uint32_t a = ex < 0 ? -ex : ex;
    printf(" = %s%c", baseName, ex < 0 ? '-' : '+');
    printFixed(a / K, a % K, K);
}

struct Lap { uint32_t T, K, trials; };

/* The second ruler holds 16 bits: what matters is that its difference is the first's, modulo 65,536. */
static int32_t worstRulers = 0;
static int32_t worse(int32_t was, uint16_t ruler2, uint32_t E) {
    const int16_t d = (int16_t)(ruler2 - (uint16_t)E);
    const int32_t a = d < 0 ? -d : d;
    if (a > worstRulers) worstRulers = a;
    return a > was ? a : was;
}

static void lapRow(const char* clock, const char* name, uint32_t mode, const Lap& l, uint32_t tick, uint32_t nominal,
                   uint32_t base, const char* baseName, uint32_t how) {
    uint32_t sumNet = 0, sumK = 0, sumE = 0, sumReads = 0;
    int32_t lowEx = 0x7FFFFFFF, highEx = -0x7FFFFFFF, rulerOff = 0;
    for (uint32_t i = 0; i < l.trials; i++) {
        Sample a, b;
        start(mode, l.T);
        take(a);
        take(a);
        sumReads += watch(how, l.K * nominal + 13 * (i & 7));   /* 13: another place in a tick each trial */
        take(b);
        const uint32_t E = b.ruler - a.ruler;
        const uint32_t net = E - tick * (uint32_t)((int32_t)b.count - (int32_t)a.count);
        const uint32_t K = (net + nominal / 2) / nominal;
        if (!K) { printf("%s T=%04x %s: under one lap watched (E %u)\n", clock, l.T, name, E); return; }
        const int32_t ex = (int32_t)net - (int32_t)(K * base);
        if (ex < lowEx) lowEx = ex;
        if (ex > highEx) highEx = ex;
        rulerOff = worse(rulerOff, (uint16_t)(b.ruler2 - a.ruler2), E);
        sumNet += net; sumK += K; sumE += E;
    }
    printf("%s T=%04x K=%3d x%2d %-11s ", clock, l.T, l.K, l.trials, name);
    printLap(sumNet, sumK, base, baseName);
    printf("  spread %d", highEx - lowEx);
    if (sumReads) { printf("  read every "); printFixed(sumE / sumReads, sumE % sumReads, sumReads); }
    if (rulerOff > RULERS_NOISE) printf("  RULERS DISAGREE by %d", rulerOff);
    printf("\n");
}

/* K laps between flag edges, the counter never read. */
static void flagRow(const char* name, uint32_t T, uint32_t K, uint32_t base, const char* baseName, int useMode) {
    const uint32_t bound = 8 * T + 4096;
    uint32_t t0 = 0, t1 = 0, sum = 0, edges = 0;
    int32_t rulerOff = 0;
    const uint32_t trials = 4;
    for (uint32_t i = 0; i < trials; i++) {
        uint16_t r0 = 0, r1 = 0;
        start(M_RESET | M_IRQT | M_REPEAT | M_DIV8, T);
        const uint16_t clear = T2_MODE;                    /* the read clears the reached bits */
        (void)clear;
        I_STAT = ~I_TMR2;
        for (uint32_t e = 0; e <= K; e++) {
            uint32_t n = bound;
            for (; n; n--) {
                if (useMode ? (T2_MODE & F_TARGET) != 0 : (I_STAT & I_TMR2) != 0) break;
                now();
            }
            if (!n) { printf("/8 T=%04x %s: no edge %d in %u polls\n", T, name, e, bound); return; }
            r1 = T0_COUNT;
            t1 = now();
            if (!useMode) I_STAT = ~I_TMR2;
            if (e == 0) { t0 = t1; r0 = r1; }
        }
        sum += t1 - t0;
        edges += K;
        rulerOff = worse(rulerOff, (uint16_t)(r1 - r0), t1 - t0);
    }
    printf("/8 T=%04x K=%3d x%2d %-11s ", T, K, trials, name);
    printLap(sum, edges, base, baseName);
    if (rulerOff > RULERS_NOISE) printf("  RULERS DISAGREE by %d", rulerOff);
    printf("\n");
}

static uint32_t hist[18];

static void dwellRow(const char* name, void (*burst)(uint32_t, uint32_t)) {
    const uint32_t T = 0x10;
    for (uint32_t i = 0; i < sizeof(hist) / sizeof(hist[0]); i++) hist[i] = 0;
    start(M_RESET | M_DIV8, T);
    uint32_t E = 0;
    for (int b = 0; b < 256; b++) {
        const uint32_t t0 = now();
        burst(SCRATCH, SCRATCH + BURST * 2);
        E += now() - t0;
        const volatile uint16_t* p = (const volatile uint16_t*)SCRATCH;
        for (int i = 0; i < BURST; i++) {
            const uint32_t v = p[i];
            hist[v <= T ? v : 17]++;
        }
    }
    uint32_t mid = 0;
    for (uint32_t v = 1; v < T; v++) mid += hist[v];
    const uint32_t reads = 256 * BURST, mean = mid / (T - 1);
    printf("DWELL T=0010 %s, a read every ", name);
    printFixed(E / reads, E % reads, reads);
    printf(" cycles\n ");
    for (uint32_t v = 0; v <= T; v++) printf(" %u", hist[v]);
    printf("  above T %u\n", hist[17]);
    printf("  in cycles, the mean value 1..F as 8: value 0 ");
    printFixed(8 * hist[0] / mean, (8 * hist[0] % mean), mean);
    printf(", value T ");
    printFixed(8 * hist[T] / mean, (8 * hist[T] % mean), mean);
    printf("\n");
}

int main() {
    printf("\ntimers/timer2-lap\n");
    printf("lap = (ruler - tick x counter) / K: Timer 1 on the system clock, Timer 0 beside it. See the header.\n\n");

    EnterCriticalSection();
    const uint32_t oldMask = I_MASK;
    I_MASK = 0;
    const uint16_t prev0 = T0_MODE, prev1 = T1_MODE, prev2 = T2_MODE, prevTgt = T2_TGT;
    T1_MODE = 0;
    T0_MODE = 0;
    lo = T1_COUNT;

    printf("CONTROLS, laps already known: reset at target on the system clock T+2 (ps1-tests timers, M104), a wrap 10000h counts\n");
    static const Lap C1 = { 0x1000, 64, 4 }, C2 = { 0x0010, 12, 4 };
    lapRow("/1", "IDLE", M_RESET, C1, 1, 0x1002, 0x1000, "T", IDLE);
    lapRow("/1", "POLL tight", M_RESET, C1, 1, 0x1002, 0x1000, "T", TIGHT);
    lapRow("/1", "IDLE", M_RESET, C2, 1, 0x12, 0x10, "T", IDLE);
    lapRow("/1", "POLL tight", M_RESET, C2, 1, 0x12, 0x10, "T", TIGHT);
    static const Lap C3 = { 0, 64, 4 }, C4 = { 0, 8, 4 };
    lapRow("/1", "wrap, IDLE", 0, C3, 1, 0x10000, 0x10000, "10000h", IDLE);
    lapRow("/1", "wrap, POLL", 0, C3, 1, 0x10000, 0x10000, "10000h", TIGHT);
    lapRow("/8", "wrap, IDLE", M_DIV8, C4, 8, 0x80000, 0x80000, "80000h", IDLE);
    lapRow("/8", "wrap, POLL", M_DIV8, C4, 8, 0x80000, 0x80000, "80000h", TIGHT);

    printf("\nTHE LAP at /8, reset at target\n");
    static const Lap L[] = { { 0x8000, 16, 4 }, { 0x1000, 64, 4 }, { 0x0100, 128, 4 }, { 0x0010, 12, 16 } };
    for (uint32_t k = 0; k < sizeof(L) / sizeof(L[0]); k++) {
        const Lap& l = L[k];
        const uint32_t nom = 8 * l.T + 4, base = 8 * l.T;
        const uint32_t m = M_RESET | M_DIV8;
        lapRow("/8", "IDLE", m, l, 8, nom, base, "8T", IDLE);
        lapRow("/8", "POLL tight", m, l, 8, nom, base, "8T", TIGHT);
        lapRow("/8", "POLL w=0", m, l, 8, nom, base, "8T", 0);
        lapRow("/8", "POLL w=10", m, l, 8, nom, base, "8T", 10);
        lapRow("/8", "POLL w=100", m, l, 8, nom, base, "8T", 100);
        flagRow("FLAG I_STAT", l.T, l.K, base, "8T", 0);
        flagRow("FLAG MODE", l.T, l.K, base, "8T", 1);
        printf("\n");
    }

    printf("DWELL at /8, reset at target: how many of the reads saw each value 0..T\n");
    dwellRow("spacing A", burstA);
    dwellRow("spacing B", burstB);
    printf("\nthe two rulers agreed within %d cycles on every row\n", worstRulers);

    T2_MODE = 0;
    T2_TGT = prevTgt;
    T2_MODE = prev2;
    T1_MODE = prev1;
    T0_MODE = prev0;
    I_STAT = ~I_TMR2;
    I_MASK = oldMask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
