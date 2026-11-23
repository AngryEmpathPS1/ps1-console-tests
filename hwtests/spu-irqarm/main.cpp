/*
 * spu/spu-irqarm - an SPU IRQ ARMED while a voice is somewhere in its block: does it fire, and when? (2026-11-23, the SPU session)
 *
 * WHY. The IRQ address is in units of 8 bytes: a 16-byte ADPCM block has two, its start and its second half. This emulator
 * tests a voice's reads as mednafen does - the block's start by ANY read of the block, the second half only by the read of its
 * first halfword (hw 4) - and, armed late (the enable or the address written with a voice already there), checks each voice's
 * LAST READ at the next sample. hwtests/spu-irq, spu-lead and spu-rearm disagree with that in ways one rule cannot serve
 * (STATUS "SPU_IRQ_VOICE_SEEN re-measured": spu-irq's armed fire 392-448 cycles on the consoles, 1,368 here; the fix for it
 * breaks the other two), and every one of them keys a voice at pitch 1000h or sits it on one halfword: none says WHERE in the
 * block the voice may be for an armed IRQ to fire.
 *
 * WHAT IT DOES. Voice 1 at pitch 0100h over silent blocks at 1000h: a source sample lasts 16 output samples, a halfword of four
 * 64, a block 448 (344,064 cycles). The IRQ address is the second block's start (1010h) or its second half (1018h). A trial: all
 * off, IRQ9 disabled and acknowledged, the address written, voice 1 keyed on right behind a Timer 2 read (tw); a scattered wait
 * of 80,000-880,000 cycles (before, through and past the second block); SPUCNT's IRQ enable written right behind a Timer 2 read
 * (te); then a loop that only stores Timer 2 and I_STAT, 4,600 turns (~70 samples: longer than a halfword lasts). Read
 * afterwards for the turn where I_STAT bit 9 first reads set.
 *   E d t0 t1          SPUSTAT bit 11 read as !d at t0 and as d at t1, consecutive reads (as spu-latch2)
 *   A a tw te lo hi    a: 0 the block's start (1010h), 1 its second half (1018h); I_STAT bit 9 clear at lo, set at hi
 *                      (consecutive reads; 0 0: not seen in the loop)
 * (te - tw) / 768 is where the voice is, in output samples from its key-on write; hi - te how long the armed IRQ took.
 * experiments/spu_irqarm.js reads it. Interrupts off, sound muted, stable sound RAM reads. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-irqarm --frames=12000 --quiet=3000`.
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

#define TURNS 4600
static uint16_t rec[TURNS * 2] __attribute__((aligned(16)));

/* kon_t2: the Timer 2 read and voice 1's KON store. arm_watch(buf, end, spucnt): the Timer 2 read, the SPUCNT store, then
 * turns of Timer 2 and I_STAT stored. Each returns its Timer 2 count. */
extern "C" { uint16_t kon_t2(void); uint16_t arm_watch(uint16_t* buf, uint16_t* end, uint32_t spucnt); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl kon_t2\n.type kon_t2,@function\nkon_t2:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".globl arm_watch\n.type arm_watch,@function\narm_watch:\n"
    "  lui $t0, 0x1f80\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $a2, 0x1daa($t0)\n"
    "1:\n"
    "  lhu $t3, 0x1120($t0)\n"
    "  lhu $t4, 0x1070($t0)\n"
    "  sh $t3, 0($a0)\n"
    "  sh $t4, 2($a0)\n"
    "  addiu $a0, $a0, 4\n"
    "  bne $a0, $a1, 1b\n"
    "  nop\n"
    "  jr $ra\n  nop\n"
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
static void armTrial(int a) {
    allOff();
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));                 /* IRQ9 off: acknowledged */
    write16(SPU(0x1A4), (uint16_t)((0x1010 + 8 * a) / 8));
    write16(SPU(0x010), 0); write16(SPU(0x012), 0);
    write16(SPU(0x014), 0x0100);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
    idle(2000u + rnd(1536)); fine(rnd(48));
    write32(I_STAT, ~0x200u);
    const uint32_t tw = stamp(kon_t2());
    idle(80000u + rnd(800000u)); fine(rnd(48));
    const uint32_t te = stamp(arm_watch(rec, rec + TURNS * 2, (uint32_t)(cnt | 0x0040)));
    uint32_t lo = 0, hi = 0, prev = te; bool seen = false;
    for (int i = 0; i < TURNS; i++) {               /* every count stamped in order: the clock stays whole */
        const uint32_t ti = stamp(rec[2 * i]);
        if (!seen && (rec[2 * i + 1] & 0x200)) { lo = prev; hi = ti; seen = true; }
        prev = ti;
    }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    write16(SPU(0x18C), 0x0002);
    now();
    printf("A %d %u %u %u %u\n", a, (unsigned)tw, (unsigned)te, (unsigned)lo, (unsigned)hi);
    now();
}

int main() {
    printf("\nspu/spu-irqarm\n");
    printf("An SPU IRQ armed with a voice somewhere in its block, on one Timer 2 count: E d t0 t1 / A a tw te lo hi. No assertions.\n\n");
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
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 128; i++) b[i] = 0;
    b[1] = 0x04;                                    /* block 0: the loop's start */
    b[16 * 4 + 1] = 0x03;                           /* block 4: end and repeat - back to block 0 */
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 32, true);

    edges();
    for (int n = 0; n < 480; n++) { armTrial(n & 1); if ((n & 127) == 127) edges(); }
    edges();
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-irqarm: Done.\n");
    for (;;) VSync(0);
}
