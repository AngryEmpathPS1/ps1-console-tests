/*
 * spu/spu-irqgap - each voice's IRQ place in the sample TO THE CYCLE, by bisection with one exact-time read a pass (2026-11-24,
 * the SPU session)
 *
 * WHY. Voice 0's IRQ leads the sample's tick; hwtests/spu-irqstat's twelve console logs put it 225.5 cycles before voice 1's
 * on the four older boards and 222.2 on the SCPH-9001 and the PSone (the means of a 34-cycle loop's readings), and a per-board
 * lead of 12 / 9 was proposed on that; the same logs had SPUSTAT bit 6 show an IRQ 13 cycles sooner on those two boards
 * (v0.377.377's spu.irqStat). This core cannot check either: its turn of that loop is 35 cycles (hwtests/cpu-loop-tick19) and
 * a sweep of the lead reads 9 and 12 alike through spu-irqstat and through spu-irqslot (steps of 8-13 cycles). A 3-cycle
 * difference needs a test with no loop in it.
 *
 * WHAT IT DOES. Interrupts off, sound muted. One voice loops over five silent blocks at 1000h at pitch 1000h - it reads the
 * IRQ address's block (1010h) once every 140 samples, 107,520 cycles, the same cycle of every pass. Each pass is a window of
 * that length on one unwrapped Timer 2 clock: the IRQ acknowledged at its start (SPUCNT bit 6 off, I_STAT bit 9 cleared, bit 6
 * on - done by 6,000 cycles in), then ONE read at an exact cycle x of the window - the last stretch counted off Timer 2 into a
 * sled of nops, so the read falls at window + x + a constant. The flag is clear before the voice's read of that pass and set
 * after it, so 17 windows bisect the cycle it turns. The clock is one for the whole run, so a place taken mod 768 is the same
 * sample grid for every voice. Four reads:
 *   I   lhu I_STAT                                              bit 9
 *   S   lhu SPUSTAT                                             bit 6
 *   P   lhu I_STAT; lhu SPUSTAT                                 SPUSTAT's bit 6 right behind an on-die read (spu-irqstat's pair)
 *   J   sh; sh; sh; nop; nop; nop; lhu Timer 2; lhu I_STAT      I_STAT's bit 9 behind three RAM stores and a Timer 2 read (its turn)
 *   G v read : x p ok       voice v; x = the first cycle of the window the read sees the bit set; p = (base + x) mod 768, its
 *                           place in the sample (plus that read's own constant); ok = 1 when the window's ends read clear and
 *                           set as they must (0: no IRQ seen; 2: it fell inside the acknowledge, the windows moved half a pass)
 * Voices 0, 1, 2, 23, 3, 12 (I twice, S), then voices 0, 1 and 23 with all four. Voices 1 and 2 are a slot apart, voice 23 22
 * slots past voice 1; voice 0 against voice 23 is the lead; P less S and J less I are the CPU's, the same for every voice.
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
#define PASS 107520u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(2);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, 0x01000201u);
    for (int i = 0; i < 400000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { printf("T %x\n", (unsigned)read32(0x1F8010C8)); write32(0x1F8010C8, 0); }
    idle(100000);
    spuMode(0);
}
static uint32_t blocks[32] __attribute__((aligned(16)));
static volatile uint16_t sink[8] __attribute__((aligned(16)));

/* probe_X(target, ram): a loop reads Timer 2 until under 160 cycles are left to `target` (its 16 bits), then jumps into a sled
 * of nops that many long less 60, and the read's own instructions follow. v0 = the halfword read (FFFFFFFFh: the target had
 * passed). $a1 = a RAM halfword for J's stores. */
#define PROBE(name, tail) \
    ".globl " name "\n.type " name ",@function\n" name ":\n" \
    "  lui $t0, 0x1f80\n" \
    "  andi $a0, $a0, 0xffff\n" \
    "1:\n" \
    "  lhu $t1, 0x1120($t0)\n" \
    "  nop\n" \
    "  subu $t2, $a0, $t1\n" \
    "  andi $t2, $t2, 0xffff\n" \
    "  sltiu $t3, $t2, 0x4000\n" \
    "  beqz $t3, 9f\n" \
    "  sltiu $t3, $t2, 160\n" \
    "  beqz $t3, 1b\n" \
    "  nop\n" \
    "  addiu $t2, $t2, -60\n" \
    "  sll $t2, $t2, 2\n" \
    "  la $t4, 8f\n" \
    "  subu $t4, $t4, $t2\n" \
    "  jr $t4\n" \
    "  nop\n" \
    "  .rept 128\n  nop\n  .endr\n" \
    "8:\n" tail \
    "  jr $ra\n  nop\n" \
    "9:\n" \
    "  li $v0, -1\n" \
    "  jr $ra\n  nop\n"
extern "C" { uint32_t probe_i(uint32_t target, volatile uint16_t* ram); uint32_t probe_s(uint32_t target, volatile uint16_t* ram);
             uint32_t probe_p(uint32_t target, volatile uint16_t* ram); uint32_t probe_j(uint32_t target, volatile uint16_t* ram); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    PROBE("probe_i", "  lhu $v0, 0x1070($t0)\n")
    PROBE("probe_s", "  lhu $v0, 0x1dae($t0)\n")
    PROBE("probe_p", "  lhu $v1, 0x1070($t0)\n  lhu $v0, 0x1dae($t0)\n")
    PROBE("probe_j", "  sh $t1, 0($a1)\n  sh $t1, 2($a1)\n  sh $t1, 4($a1)\n  nop\n  nop\n  nop\n  lhu $v1, 0x1120($t0)\n  lhu $v0, 0x1070($t0)\n")
    ".set pop\n");
typedef uint32_t (*Probe)(uint32_t, volatile uint16_t*);
static const Probe PROBES[4] = { probe_i, probe_s, probe_p, probe_j };
static const char KIND[4] = { 'I', 'S', 'P', 'J' };
static const uint32_t MASK[4] = { 0x0200u, 0x0040u, 0x0040u, 0x0200u };

static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
}
static void voiceSet(int v, uint16_t pitch, uint32_t addr) {
    const uint32_t o = (uint32_t)v * 16;
    write16(SPU(o + 0), 0); write16(SPU(o + 2), 0);
    write16(SPU(o + 4), pitch);
    write16(SPU(o + 6), (uint16_t)(addr / 8));
    write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
}
/* Every voice into the loop at 1800h, and off: a silent voice goes on reading its blocks, so none is left near the IRQ address. */
static void park() {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    for (int v = 0; v < 24; v++) voiceSet(v, 0x1000, 0x1800);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    write16(SPU(0x188), 0xFFFF); write16(SPU(0x18A), 0x00FF);
    idle(8000);
    allOff();
}

static uint32_t base;
/* One window: the IRQ acknowledged at its start, the kind's read at its cycle x. The value read (FFFFFFFFh: it came late). */
static uint32_t window(uint32_t x, int kind) {
    const uint32_t n = (now() - base) / PASS + 1, start = base + n * PASS;
    while ((int32_t)(start - now()) > 0) {}
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    idle(2500);
    write32(I_STAT, ~0x200u);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const uint32_t target = start + x;
    while ((int32_t)(target - now()) > 1200) {}
    const uint32_t v = PROBES[kind](target & 0xFFFF, sink);
    now();
    return v;
}
static bool seen(uint32_t x, int kind) {
    for (int tries = 0; tries < 4; tries++) {
        const uint32_t v = window(x, kind);
        if (v != 0xFFFFFFFFu) return (v & MASK[kind]) != 0;
    }
    return false;
}
static void bisect(int v, int kind) {
    int ok = 1;
    uint32_t lo = 6000, hi = PASS - 1;
    if (!seen(hi, kind)) ok = 0;
    else if (seen(lo, kind)) {                      /* the voice's read falls inside the acknowledge: half a pass on */
        base += PASS / 2; ok = 2;
        if (!seen(hi, kind) || seen(lo, kind)) ok = 0;
    }
    if (ok) while (hi - lo > 1) { const uint32_t mid = lo + (hi - lo) / 2; if (seen(mid, kind)) hi = mid; else lo = mid; }
    now();
    printf("G %d %c : %u %u %d\n", v, KIND[kind], (unsigned)hi, (unsigned)((base + hi) % 768u), ok);
    now();
}
static void voice(int v, bool all) {
    park();
    write16(SPU(0x1A4), 0x1010 / 8);
    voiceSet(v, 0x1000, 0x1000);
    if (v < 16) write16(SPU(0x188), (uint16_t)(1u << v)); else write16(SPU(0x18A), (uint16_t)(1u << (v - 16)));
    idle(768u * 400u);
    bisect(v, 0);
    bisect(v, 0);
    bisect(v, 1);
    if (all) { bisect(v, 2); bisect(v, 3); bisect(v, 2); bisect(v, 3); }
}

int main() {
    printf("\nspu/spu-irqgap\n");
    printf("Each voice's IRQ place in the sample to the cycle, by bisection: G v read : x p ok. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 128; i++) b[i] = 0;
    b[1] = 0x04;                                    /* block 0: the loop's start */
    b[16 * 4 + 1] = 0x03;                           /* block 4: end and repeat - back to block 0 */
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 32);
    b[16 * 4 + 1] = 0;
    b[16 * 1 + 1] = 0x03;                           /* the parking loop: two blocks */
    spuDma(0x1800, blocks, 32);
    for (int k = 0; k < 4; k++) for (int i = 0; i < 4; i++) { PROBES[k]((now() + 3000u) & 0xFFFF, sink); now(); }   /* the routines into the cache */
    base = now();

    static const int V[] = { 0, 1, 2, 23, 3, 12 };
    for (unsigned i = 0; i < sizeof(V) / sizeof(V[0]); i++) voice(V[i], false);
    voice(0, true); voice(1, true); voice(23, true);

    park();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-irqgap: Done.\n");
    for (;;) VSync(0);
}
