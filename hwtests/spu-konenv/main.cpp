/*
 * spu/spu-konenv - when a keyed-on voice's envelope takes its first steps, against the key-on write, to the cycle (2026-11-23,
 * the SPU session)
 *
 * WHY. hwtests/spu-regs times each envelope from an unsynchronised key-on write with ENVX read every 256 cycles: a row is right
 * or a sample off by where the write fell in the sample, which nobody knows. On six consoles (2026-11-23, boot-probed) its
 * linear rate 10 and 18 rows are the same on every console and one sample later on this emulator at every draw tried, its rate
 * 00 rows differ console to console - SPU_KON_WINDOW's KNOWN-OPEN cannot be closed on that instrument. hwtests/spu-latch2 and
 * spu-konq place the KON LATCH by the capture ring's onset; nothing places the ENVELOPE's first steps on the same count.
 *
 * WHAT IT DOES. As spu-konq's trial with a lone key-on: voice 1 keyed on right behind a Timer 2 read at a scattered time; then
 * a loop that only stores Timer 2 and voice 1's ENVX (1F801C1Ch) into an array, 420 turns (~17 samples). Read afterwards: the
 * first four changes of ENVX, each with the Timer 2 read just ahead of the ENVX read that first showed it. Four attack rates:
 * linear 00, 10 and 18 and exponential 08 (ADSR low word rate << 8 | 000Fh, as spu-regs).
 *   E d t0 t1                     SPUSTAT bit 11 read as !d at t0 and as d at t1, consecutive reads (as spu-latch2)
 *   V r tw t1 v1 t2 v2 t3 v3 t4 v4   rate r (hex; 88 = exponential 08): the key-on's Timer 2 read tw, then each change's time
 *                                 and the ENVX value read (hex); fewer than four changes: the rest 0 0
 * experiments/spu_konenv.js reads it: t1 - tw against tw's phase in the two-sample cycle, per rate.
 * Interrupts off, sound muted, stable sound RAM reads (220931E1h). Every wait is bounded. There are no assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-konenv --frames=9000 --quiet=3000`.
 */
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
    if (toSpu) idle(8000);
    spuMode(0);
}

static uint32_t blocks[16] __attribute__((aligned(16)));
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }

/* 0-47 cycles more, one at a time (spu-latch2: the waits above end on a poll loop's turn, tied to the sample clock) */
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}

#define TURNS 420
static uint16_t rec[TURNS * 2] __attribute__((aligned(16)));

/* The Timer 2 read and the KON store, then TURNS turns of: Timer 2, voice 1's ENVX, both stored. Returns the first count. */
extern "C" { uint16_t kon_env(uint16_t* buf, uint16_t* end); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl kon_env\n.type kon_env,@function\nkon_env:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "1:\n"
    "  lhu $t3, 0x1120($t0)\n"
    "  lhu $t4, 0x1c1c($t0)\n"
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
static void envTrial(unsigned rate) {
    allOff();
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);
    write16(SPU(0x014), 0x1000);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), (uint16_t)(((rate & 0x80) ? 0x8000 : 0) | ((rate & 0x7F) << 8) | 0x000F)); write16(SPU(0x01A), 0x0000);
    idle(2000u + rnd(1536)); fine(rnd(48));
    const uint16_t t = kon_env(rec, rec + TURNS * 2);
    const uint32_t tw = stamp(t);
    uint32_t ct[4] = { 0, 0, 0, 0 }; uint16_t cv[4] = { 0, 0, 0, 0 };
    int n = 0; uint16_t last = 0;
    for (int i = 0; i < TURNS; i++) {               /* every count stamped in order: the clock stays whole */
        const uint32_t ti = stamp(rec[2 * i]);
        const uint16_t v = rec[2 * i + 1];
        if (v != last && n < 4) { ct[n] = ti; cv[n] = v; n++; }
        last = v;
    }
    write16(SPU(0x18C), 0x0002);
    now();
    printf("V %02x %u %u %x %u %x", rate, (unsigned)tw, (unsigned)ct[0], cv[0], (unsigned)ct[1], cv[1]);
    now();
    printf(" %u %x %u %x\n", (unsigned)ct[2], cv[2], (unsigned)ct[3], cv[3]);
    now();
}

int main() {
    printf("\nspu/spu-konenv\n");
    printf("A keyed-on voice's first envelope steps, on one Timer 2 count: E d t0 t1 / V rate tw t1 v1 t2 v2 t3 v3 t4 v4. No assertions.\n\n");
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
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 16, true);

    edges();
    static const unsigned RATES[4] = { 0x00, 0x10, 0x18, 0x88 };
    for (int n = 0; n < 640; n++) { envTrial(RATES[n & 3]); if ((n & 127) == 127) edges(); }
    edges();
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-konenv: Done.\n");
    for (;;) VSync(0);
}
