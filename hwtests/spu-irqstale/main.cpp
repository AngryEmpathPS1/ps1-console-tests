/*
 * spu/spu-irqstale - SPUSTAT bit 6 after an ACKNOWLEDGE and a NEW IRQ a moment later: does the old flag still show? (2026-11-23, the
 * SPU session)
 *
 * WHY. hwtests/spu-irqstat on six consoles: SPUSTAT bit 6 takes the IRQ flag at one place a sample, whatever raised it - a
 * transfer address's IRQ shows 146-758 cycles after I_STAT bit 9. hwtests/spu-irq's row "IRQ address 2100h, then the transfer
 * address set to 2100h" reads 24-32 cycles on every console (60 logs), which v0.377.208 took as "a transfer's flag shows at once".
 * That row comes ~300 cycles after the row before's acknowledge: if bit 6 goes on showing an acknowledged flag until its next
 * update (hwtests/spu-rearm: 675-760 cycles), the row read the OLD flag. This parts the two.
 *
 * WHAT IT DOES. spu-irqstat's set-up. A trial: an IRQ on the transfer address (2100h) raised and left 3,000 cycles (shown);
 * acknowledged - SPUCNT's enable cleared right behind a Timer 2 read (ta) - and the transfer address moved away; a scattered wait
 * D of 0-1,799 cycles; I_STAT acknowledged, the enable set, and the transfer address set onto 2100h again right behind a Timer 2
 * read (te), then spu-irqstat's loop (Timer 2, I_STAT, SPUSTAT stored, 1,200 turns).
 *   E d t0 t1               SPUSTAT bit 11 read as !d at t0 and as d at t1, consecutive reads
 *   R ta te lo hi st z      I_STAT bit 9 clear in the turn stamped lo, set in hi; SPUSTAT bit 6 first SET in the turn stamped st and
 *                           first CLEAR in the turn stamped z (0: never in the loop)
 * If the old flag shows until bit 6's next update: st is the loop's first turn whenever no update fell between ta and the new
 * IRQ (te - ta under a sample: the nearer, the more often), and z is 0 there. If a flag shows at once: st = hi's turn always.
 * experiments/spu_irqstale.js reads it. 600 trials. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-irqstale --frames=12000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

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
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 400000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { printf("T %x\n", (unsigned)read32(0x1F8010C8)); write32(0x1F8010C8, 0); }
    if (toSpu) idle(100000);
    spuMode(0);
}

static uint32_t blocks[32] __attribute__((aligned(16)));
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }

static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}

#define TURNS 1200
static uint16_t rec[TURNS * 3] __attribute__((aligned(16)));

/* kon_t2: the Timer 2 read and voice 1's KON store. st_watch(buf, end, value, reg): the Timer 2 read, the halfword store of
 * value at 1F800000h + reg, then turns of Timer 2, I_STAT and SPUSTAT stored. Each returns its Timer 2 count. */
extern "C" { uint16_t kon_t2(void); uint16_t st_watch(uint16_t* buf, uint16_t* end, uint32_t value, uint32_t reg); uint16_t st_one(uint32_t value, uint32_t reg); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl kon_t2\n.type kon_t2,@function\nkon_t2:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".globl st_watch\n.type st_watch,@function\nst_watch:\n"
    "  lui $t0, 0x1f80\n"
    "  addu $t1, $t0, $a3\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $a2, 0($t1)\n"
    "1:\n"
    "  lhu $t3, 0x1120($t0)\n"
    "  lhu $t4, 0x1070($t0)\n"
    "  lhu $t5, 0x1dae($t0)\n"
    "  sh $t3, 0($a0)\n"
    "  sh $t4, 2($a0)\n"
    "  sh $t5, 4($a0)\n"
    "  addiu $a0, $a0, 6\n"
    "  bne $a0, $a1, 1b\n"
    "  nop\n"
    "  jr $ra\n  nop\n"
    /* st_one(value, reg): the Timer 2 read and the halfword store of value at 1F800000h + reg. Returns the Timer 2 count. */
    ".globl st_one\n.type st_one,@function\nst_one:\n"
    "  lui $t0, 0x1f80\n"
    "  addu $t1, $t0, $a1\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  jr $ra\n"
    "  sh $a0, 0($t1)\n"
    ".set pop\n");

static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
}
static void edges() {
    for (int n = 0; n < 6; n++) {
        const uint32_t t0 = now();
        int s0 = (read16(SPU(0x1AE)) >> 11) & 1;
        uint32_t ta = now();
        for (;;) {
            const int s = (read16(SPU(0x1AE)) >> 11) & 1;
            const uint32_t tb = now();
            if (s != s0) { printf("E %d %u %u\n", s ? 1 : 0, (unsigned)ta, (unsigned)tb); break; }
            ta = tb;
            if (tb - t0 > 600000u) { printf("E timeout\n"); return; }
        }
    }
}
static void voiceSet(int v, uint16_t pitch, uint32_t addr) {
    const uint32_t o = (uint32_t)v * 16;
    write16(SPU(o + 0), 0); write16(SPU(o + 2), 0);
    write16(SPU(o + 4), pitch);
    write16(SPU(o + 6), (uint16_t)(addr / 8));
    write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
}
/* Every voice into the loop at 1800h, and off: a silent voice goes on reading its blocks, so none is left near an IRQ address. */
static void park() {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));                 /* IRQ9 off: acknowledged */
    for (int v = 0; v < 24; v++) voiceSet(v, 0x1000, 0x1800);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    write16(SPU(0x188), 0xFFFF); write16(SPU(0x18A), 0x00FF);
    idle(8000);
    allOff();
}
static void trial() {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));                 /* IRQ9 off: acknowledged */
    write16(SPU(0x1A4), 0x2100 / 8);
    write16(SPU(0x1A6), 0x2400 / 8);
    idle(2000u + rnd(1536)); fine(rnd(48));
    write32(I_STAT, ~0x200u);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    write16(SPU(0x1A6), 0x2100 / 8);                                /* the first IRQ */
    idle(3000u + rnd(1536)); fine(rnd(48));
    const uint32_t ta = stamp(st_one((uint32_t)(cnt & ~0x0040), 0x1daa));   /* acknowledged */
    write16(SPU(0x1A6), 0x2400 / 8);
    { const uint32_t d = rnd(1800); if (d >= 60) idle(d); else fine(d); }
    fine(rnd(48));
    write32(I_STAT, ~0x200u);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const uint32_t te = stamp(st_watch(rec, rec + TURNS * 3, 0x2100 / 8, 0x1da6));
    uint32_t lo = 0, hi = 0, st = 0, z = 0, prev = te; bool seenI = false, seenS = false, seenZ = false;
    for (int i = 0; i < TURNS; i++) {               /* every count stamped in order: the clock stays whole */
        const uint32_t ti = stamp(rec[3 * i]);
        if (!seenI && (rec[3 * i + 1] & 0x200)) { lo = prev; hi = ti; seenI = true; }
        if (!seenS && (rec[3 * i + 2] & 0x040)) { st = ti; seenS = true; }
        if (!seenZ && !(rec[3 * i + 2] & 0x040)) { z = ti; seenZ = true; }
        prev = ti;
    }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    now();
    printf("R %u %u %u %u %u %u\n", (unsigned)ta, (unsigned)te, (unsigned)lo, (unsigned)hi, (unsigned)st, (unsigned)z);
    now();
}

int main() {
    printf("\nspu/spu-irqstale\n");
    printf("SPUSTAT bit 6 after an acknowledge and a new IRQ a moment later: E d t0 t1 / R ta te lo hi st z. No assertions.\n\n");
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
    write16(SPU(0x1A2), 0xFFFE);                    /* the reverb's area: the top 16 bytes */
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 128; i++) b[i] = 0;
    b[1] = 0x04;                                    /* block 0: the loop's start */
    b[16 * 4 + 1] = 0x03;                           /* block 4: end and repeat - back to block 0 */
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 32, true);
    b[16 * 4 + 1] = 0;
    b[16 * 1 + 1] = 0x03;                           /* the parking loop: two blocks */
    spuDma(0x1800, blocks, 32, true);

    park();
    edges();
    for (int n = 0; n < 600; n++) {
        trial();
        if ((n & 127) == 127) edges();
    }
    edges();
    park();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-irqstale: Done.\n");
    for (;;) VSync(0);
}
