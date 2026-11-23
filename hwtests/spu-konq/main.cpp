/*
 * spu/spu-konq - does a key-on written BEHIND other SPU stores reach the SPU later? (2026-11-23, the SPU session)
 *
 * WHY. An SPU register store takes its turn in the CPU's write queue: its access is 4 cycles and the next one's starts 8 after
 * (STATUS "An SPU register store's turn in the write queue", experiments/land_spuwq.js). A store behind Q others is then on the
 * bus about 8 Q cycles after the CPU issued it - and this emulator hands every store's value to the SPU at the issue. If the chip
 * takes the value at the store's own access, a key-on written as the last of a burst (a sound driver's voice set-up, then KON) is
 * latched later than the core has it. hwtests/spu-latch2's K rows place the KON latch in the sample to 2-3 cycles with a LONE
 * store; this runs the same trial with Q = 0, 2, 4 and 6 stores back to back ahead of it.
 *
 * WHAT IT DOES. As spu-latch2's konTrial: voice 1 keyed on right behind a Timer 2 read at a scattered time, its capture ring read
 * back, the onset's ring index printed. The Timer 2 read, the Q stores (voices 5-7's volumes, zero: they sound nothing) and the
 * KON store are consecutive instructions (kon_q0 .. kon_q6 below, written out so the compiler cannot reorder them).
 *   E d t0 t1      SPUSTAT bit 11 read as !d at t0 and as d at t1, consecutive reads (as spu-latch2)
 *   K q tw i       Q = q stores ahead of the KON; tw the Timer 2 read just ahead of them; i the onset's ring index (-1 none)
 * The KON latch's place is where i steps as tw moves through the sample (experiments/spu_konq.js): if the store's VALUE waits
 * for its turn, the step comes ~8 cycles earlier in tw for each store ahead; if not, only the instructions' own 1 cycle each.
 * Interrupts off, sound muted, stable sound RAM reads (220931E1h). Every wait is bounded. There are no assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-konq --frames=9000 --quiet=3000`.
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
static uint16_t ring[512] __attribute__((aligned(16)));
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }

/* 0-47 cycles more, one at a time (spu-latch2: the waits above end on a poll loop's turn, tied to the sample clock) */
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}

/* The Timer 2 read, Q stores of zero to voices 5-7's volumes, the KON store: consecutive instructions. Returns the count read. */
extern "C" { uint16_t kon_q0(void); uint16_t kon_q2(void); uint16_t kon_q4(void); uint16_t kon_q6(void); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl kon_q0\n.type kon_q0,@function\nkon_q0:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".globl kon_q2\n.type kon_q2,@function\nkon_q2:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $zero, 0x1c50($t0)\n  sh $zero, 0x1c52($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".globl kon_q4\n.type kon_q4,@function\nkon_q4:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $zero, 0x1c50($t0)\n  sh $zero, 0x1c52($t0)\n  sh $zero, 0x1c60($t0)\n  sh $zero, 0x1c62($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".globl kon_q6\n.type kon_q6,@function\nkon_q6:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $zero, 0x1c50($t0)\n  sh $zero, 0x1c52($t0)\n  sh $zero, 0x1c60($t0)\n  sh $zero, 0x1c62($t0)\n  sh $zero, 0x1c70($t0)\n  sh $zero, 0x1c72($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".set pop\n");

static void voiceSetup() {
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);
    write16(SPU(0x014), 0x1000);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
}
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
static void konTrial(int q) {
    allOff();
    for (int i = 0; i < 256; i++) ((uint32_t*)ring)[i] = 0;
    spuDma(0x0800, (uint32_t*)ring, 256, true);
    voiceSetup();
    idle(2000u + rnd(1536)); fine(rnd(48));
    const uint16_t t = q == 0 ? kon_q0() : q == 2 ? kon_q2() : q == 4 ? kon_q4() : kon_q6();
    const uint32_t tw = stamp(t);
    idle(120u * 768u);                              /* the onset written, well inside the ring's 512 */
    spuDma(0x0800, (uint32_t*)ring, 256, false);
    write16(SPU(0x18C), 0x0002);
    int onset = -1;
    for (int i = 0; i < 512 && onset < 0; i++) {    /* the first non-zero index with 40 zeros before it (spu-latch2) */
        if (!ring[i]) continue;
        int z = 0;
        while (z < 40 && !ring[(i + 511 - z) & 511]) z++;
        if (z == 40) onset = i;
    }
    printf("K %d %u %d\n", q, (unsigned)tw, onset);
}

int main() {
    printf("\nspu/spu-konq\n");
    printf("A key-on behind Q SPU stores, on one Timer 2 count: E d t0 t1 / K q tw onset. No assertions.\n\n");
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
    static const int QS[4] = { 0, 2, 4, 6 };
    for (int n = 0; n < 960; n++) { konTrial(QS[n & 3]); if ((n & 255) == 255) edges(); }
    edges();
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-konq: Done.\n");
    for (;;) VSync(0);
}
