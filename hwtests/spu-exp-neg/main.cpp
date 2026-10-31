/*
 * spu/spu-exp-neg - where an Exponential Decrease sweep stops, from a negative volume (2026-10-30).
 *
 * WHY. psx-spx's dev text (commit 5c195d5, in the section that covers the main and the voice volumes alike) says an
 * Exponential Decrease from a negative volume stops at -0FFFh (F001h), the scaled step rounding to zero below 1000h in
 * magnitude, where a positive volume goes on down past 0FFFh; and, with nocash's text, that the Phase bit seems to have no
 * effect in this mode. This core, like DuckStation, goes to 0000h at the first step, whatever the Phase bit. Mednafen
 * (SPU_Sweep::Clock) is neither: Phase 0 snaps to 0000h, Phase 1 runs on and rounds to zero. So the Phase bit is the question
 * as much as the stop. And a stop by rounding belongs to a rate, not to a level: the step is S * level >> 15, with S = 8 - step
 * shifted left by 11 - shift below shift 11, and it is zero once S * |level| < 8000h, so the stop is -(7FFFh / S). That is
 * -0FFFh only at S = 8. Nothing here has read a sweep from a negative level; spu-curvol's all start at 0 (finding 16).
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped; main volume 0 where a voice is under test, every voice
 * silent where the main volume is: nothing is heard. Each row: every voice off, the volume fixed at its start (a fixed volume
 * reads back twice its register and is there by two samples: finding 16), the current level read, SPUSTAT bit 11's falling
 * edge and 300 cycles (spu-curvol's anchor, mid-sample), the sweep written left then right, and the current levels (1F801E00h
 * +N*4 and +2; 1F801DB8h and DBAh for the main volume) read in a loop until neither has moved for 512 samples, at most
 * 40,000,000 cycles. Left and right start at different levels. Each row prints when it is over, between measurements: rows
 * by rounding take 9 seconds together, and a run that printed only at the end would read as a hang on the way to its best
 * result. The print empties the cache, so each row runs behind a short warm-up row of its own.
 *   EXP DEC  the claim: from C000h / E000h, Phase 0 and 1.
 *            voice 1 playing a DC block: rates 00h 10h 20h 28h 2Ch 2Dh 2Eh 2Fh 30h 34h
 *            voice 5 keyed off: rates 20h 2Ch 30h          main volume: rates 20h 2Ch 30h
 *   INSIDE   from F800h / F000h, rate 2Ch, Phase 0 and 1: inside the claimed stop nothing should move, from F000h one step.
 *   POSITIVE the claim's other half: from 4000h / 2000h, rates 20h and 2Ch, Phase 0 and 1, on past 0FFFh to 0000h.
 *   LIN DEC  a control: from C000h / E000h, rates 20h and 2Ch, Phase 0 and 1, voice 1; rate 20h on the main volume. psx-spx:
 *            Phase 1 climbs to 0000h and stops; Phase 0 its pseudo code clamps to 0 at the first step.
 *   EXP INC  a control, the same rows. psx-spx: Phase 0 climbs through 0 to 7FFFh, slowing at 6000h; Phase 1 goes down to
 *            -8000h.
 * Every wait is bounded by Timer 2 and by a count of turns (rule 1).
 *
 * READ IT LIKE THIS. A row is the sweep register, then for left and right: pre (the level before the write, which must read
 * the fixed volume's) -> where it stopped, the sample of its last step, the number of steps seen; and `checks`, the level 1,
 * 2, 4 ... samples after the write, read 300 cycles into the sample as spu-curvol's are, as many as the row lasted (it ends 512
 * samples after its last step). TIMED OUT means it was still moving. A snap is one step, within a tick of the write. A stop by
 * rounding is hundreds of steps, and rate by rate it reads, left and right alike:
 *     rate    00h  10h  20h  28h  2Ch  2Dh  2Eh  2Fh  30h  34h
 *     stop   FFFF FFE1 FE01 F801 F001 EDB7 EAAB E667 F001 F001
 * F001h on every row means psx-spx's sentence is exact and this S is not the mechanism. These stops mean the rounding, and
 * F001h is one case of it. Phase 0 and Phase 1 differing means mednafen has it and the Phase sentence does not hold for
 * negative volumes. A control that does not move says the row proves nothing. POSITIVE reaching 0000h is the sign of the
 * shift: >> 15 rounds a positive level's negative step away from zero, so it never stalls.
 *
 * THE RULE: run here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js spu-exp-neg --raw --frames=4000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SPUSTAT 0x1AE
#define SAMPLE 768u
#define OFFSET 300u                                 /* cycles into the sample, from bit 11's edge: spu-curvol's anchor */
#define QUIET (512u * SAMPLE)                       /* the slowest row here steps every 4 samples */
#define WARM_QUIET (8u * SAMPLE)                    /* a warm-up row: rate 00h is over in 16 samples at the most */
#define LIMIT 40000000u                             /* cycles a row: rate 34h by rounding takes about 23,000,000 */
#define EDGE_LIMIT 2000000u
#define CHECKS 15                                   /* samples 1, 2, 4 ... 16384 */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
/* Until c cycles after t0. A turn is well over a cycle, so the count ends it only if Timer 2 has stopped (rule 1). */
static void until(uint32_t t0, uint32_t c) { for (uint32_t i = 0; i <= c && now() - t0 < c; i++) {} }
static void idle(uint32_t c) { until(now(), c); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    w16(0x1AA, cnt);
    for (int i = 0; i < 100000 && (r16(SPUSTAT) & 0x3F) != (cnt & 0x3F); i++) {}
}
/* DMA4 into sound RAM in 16-word blocks, settled before the Stop (spu-dma). */
static void spuUpload(uint32_t spuAddr, uint32_t* ram, int words) {
    spuMode(0);
    w16(0x1A6, (uint16_t)(spuAddr / 8));
    spuMode(2);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, 0x01000201u);
    for (int i = 0; i < 200000 && (read32(0x1F8010C8) & 0x01000000u); i++) {}
    idle(100000);
    spuMode(0);
}
/* SPUSTAT bit 11 falling: false if it never does. */
static bool bit11(uint16_t want, uint32_t t0) {
    for (uint32_t i = 0; i < EDGE_LIMIT; i++) {
        if ((r16(SPUSTAT) & 0x0800) == want) return true;
        if (now() - t0 > EDGE_LIMIT) return false;
    }
    return false;
}
static bool edge(uint32_t* at) {
    const uint32_t t0 = now();
    if (!bit11(0x0800, t0) || !bit11(0, t0)) return false;
    *at = now();
    return true;
}

static uint32_t blocks[16] __attribute__((aligned(16)));

static void allOff() {
    w16(0x188, 0); w16(0x18A, 0);
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    idle(100000);
}
static void spuOn() {
    cnt = 0xC000;
    w16(0x1AA, cnt);
    w16(0x180, 0); w16(0x182, 0);                               /* main volume 0: nothing heard */
    w16(0x184, 0); w16(0x186, 0);
    w16(0x190, 0); w16(0x192, 0); w16(0x194, 0); w16(0x196, 0); w16(0x198, 0); w16(0x19A, 0);
}

enum { PLAYING, KEYED_OFF, MAIN };
static const char* const WHERE[3] = { "voice 1 playing", "voice 5 keyed off", "main volume" };

/* A fixed volume's register: half the level, in 15 bits. */
#define FIXED(level) ((uint16_t)(((int)(level) >> 1) & 0x7FFF))
#define SWEEP(exp, dec, phase, rate) ((uint16_t)(0x8000 | ((exp) << 14) | ((dec) << 13) | ((phase) << 12) | (rate)))

struct Row {
    uint8_t where, noEdge, timedOut, checks;
    uint16_t startL, startR, sweep;
    uint16_t pre[2], end[2], check[2][CHECKS];
    uint32_t last[2], steps[2];
};

static void run(Row& R, uint32_t quiet) {
    allOff(); spuOn();
    uint32_t regL = 0x180, regR = 0x182, curL = 0x1B8, curR = 0x1BA;
    if (R.where != MAIN) {
        const uint32_t v = R.where == PLAYING ? 1 : 5, b = v * 16;
        regL = b; regR = b + 2; curL = 0x200 + v * 4; curR = curL + 2;
        w16(b + 0x4, 0x1000);                                   /* the DC block, held: spu-curvol's voice */
        w16(b + 0x6, 0x1000 / 8);
        w16(b + 0x8, 0x000F); w16(b + 0xA, 0x1FC0);
        if (R.where == PLAYING) { w16(0x188, (uint16_t)(1 << v)); idle(16 * SAMPLE); }
    }
    w16(regL, FIXED((int16_t)R.startL)); w16(regR, FIXED((int16_t)R.startR));
    idle(4 * SAMPLE);                                           /* finding 16: there by two samples */
    uint16_t last[2] = { r16(curL), r16(curR) };
    R.pre[0] = last[0]; R.pre[1] = last[1];
    uint32_t te = now();
    R.noEdge = edge(&te) ? 0 : 1;
    until(te, OFFSET);
    w16(regL, R.sweep); w16(regR, R.sweep);
    /* A check is read where spu-curvol reads, OFFSET into its sample. A sample's tick right between two reads would show
     * left and right a step apart (this loop's first version, reading from the write, did on this core). */
    uint32_t moved = OFFSET, next = OFFSET + SAMPLE;
    int k = 0;
    R.steps[0] = R.steps[1] = 0; R.last[0] = R.last[1] = 0; R.timedOut = 1;
    for (uint32_t i = 0; i < LIMIT; i++) {
        const bool check = k < CHECKS && now() - te + 200 >= next;
        if (check) until(te, next);
        const uint16_t l = r16(curL), r = r16(curR);
        const uint32_t t = now() - te;
        if (l != last[0]) { last[0] = l; R.steps[0]++; R.last[0] = t - OFFSET; moved = t; }
        if (r != last[1]) { last[1] = r; R.steps[1]++; R.last[1] = t - OFFSET; moved = t; }
        if (check) { R.check[0][k] = l; R.check[1][k] = r; k++; next = OFFSET + (SAMPLE << k); }
        if (t - moved >= quiet) { R.timedOut = 0; break; }
        if (t >= LIMIT) break;
    }
    R.checks = (uint8_t)k;
    R.end[0] = last[0]; R.end[1] = last[1];
    w16(regL, 0); w16(regR, 0);
}

static const char* const GROUP[5] = {
    "EXP DEC, the claim, from C000h / E000h", "INSIDE, from F800h / F000h, Exponential Decrease",
    "POSITIVE, from 4000h / 2000h, Exponential Decrease", "LIN DEC, a control, from C000h / E000h",
    "EXP INC, a control, from C000h / E000h" };

static void printSide(const Row& R, int side) {
    printf("    %s pre %04x -> %04x, last step sample %5d, steps %5d, checks", side ? "R" : "L", R.pre[side], R.end[side],
           (int)(R.last[side] / SAMPLE), (int)R.steps[side]);
    for (int k = 0; k < R.checks; k++) printf(" %04x", R.check[side][k]);
    printf("\n");
}

/* A row: the warm-up, the row, its lines. */
static void add(uint8_t group, uint8_t where, uint16_t startL, uint16_t startR, uint16_t sweep) {
    static Row R;
    static int lastGroup = -1;
    R.where = where; R.startL = 0xC000; R.startR = 0xE000; R.sweep = SWEEP(1, 1, 0, 0x00);
    run(R, WARM_QUIET);
    R.startL = startL; R.startR = startR; R.sweep = sweep;
    run(R, QUIET);
    if (group != lastGroup) { lastGroup = group; printf("\n%s:\n", GROUP[group]); }
    printf("  %s sweep %04x (rate %02x, phase %d)%s%s\n", WHERE[where], sweep, sweep & 0x7F, (sweep >> 12) & 1,
           R.noEdge ? "  NO BIT 11 EDGE" : "", R.timedOut ? "  TIMED OUT" : "");
    printSide(R, 0);
    printSide(R, 1);
}

int main() {
    printf("\nspu/spu-exp-neg\n");
    printf("Where a volume sweep stops from a negative level: the current level read until it stops moving, left and right,\n");
    printf("the voice's and the main volume's. Levels in hex; a sample is 768 cycles. No assertions.\n\n");
    printf("checks: the level at samples 1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 after the sweep's write\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                  /* DMA4 on */
    w16(0x1AC, 0x0004);                                         /* SPUSTAT bit 11 needs 1F801DACh bit 2 or 3 */
    uint8_t* bb = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) bb[i] = 0;
    bb[1] = 0x07;                                               /* loop start + repeat on itself */
    for (int i = 2; i < 16; i++) bb[i] = 0x44;                  /* 4000h every sample */
    allOff(); spuOn();
    spuUpload(0x1000, blocks, 16);

    static const uint8_t RATES[10] = { 0x00, 0x10, 0x20, 0x28, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x34 };
    static const uint8_t FEW[3] = { 0x20, 0x2C, 0x30 };
    for (int p = 0; p < 2; p++) for (int i = 0; i < 10; i++) add(0, PLAYING, 0xC000, 0xE000, SWEEP(1, 1, p, RATES[i]));
    for (int p = 0; p < 2; p++) for (int i = 0; i < 3; i++) add(0, KEYED_OFF, 0xC000, 0xE000, SWEEP(1, 1, p, FEW[i]));
    for (int p = 0; p < 2; p++) for (int i = 0; i < 3; i++) add(0, MAIN, 0xC000, 0xE000, SWEEP(1, 1, p, FEW[i]));
    for (int p = 0; p < 2; p++) add(1, PLAYING, 0xF800, 0xF000, SWEEP(1, 1, p, 0x2C));
    for (int p = 0; p < 2; p++) for (int r = 0x20; r <= 0x2C; r += 12) add(2, PLAYING, 0x4000, 0x2000, SWEEP(1, 1, p, r));
    for (int p = 0; p < 2; p++) for (int r = 0x20; r <= 0x2C; r += 12) add(3, PLAYING, 0xC000, 0xE000, SWEEP(0, 1, p, r));
    for (int p = 0; p < 2; p++) add(3, MAIN, 0xC000, 0xE000, SWEEP(0, 1, p, 0x20));
    for (int p = 0; p < 2; p++) for (int r = 0x20; r <= 0x2C; r += 12) add(4, PLAYING, 0xC000, 0xE000, SWEEP(1, 0, p, r));
    for (int p = 0; p < 2; p++) add(4, MAIN, 0xC000, 0xE000, SWEEP(1, 0, p, 0x20));
    allOff();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-exp-neg: Done.\n");
    for (;;) VSync(0);
}
