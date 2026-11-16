/*
 * hwtests/spu-capirq-common.h - shared body of spu-capirqen and spu-capirqhw (2026-11-16, the SPU session).
 *
 * WHY. Redux's spu-dbuf rep 3 arms an IRQ at the voice-1 capture ring's start (800h) 341 cycles before the ring writes it: this
 * emulator fires the IRQ on that write, five consoles do not (STATUS "spu-dbuf's rep 3, traced"). Two rules would explain it and
 * neither is measured: (EN) the capture write sees SPUCNT's IRQ enable only some time after the SPUCNT write; (HW) a capture write
 * raises the IRQ only at the IRQ address's own halfword, not anywhere in its 8-byte block. One test each:
 *   spu-capirqen  the enable written D = 0-3,071 cycles BEFORE the ring's write of 800h: is that write's IRQ taken?
 *   spu-capirqhw  the enable written 50-3,000 cycles AFTER it, while the ring writes 802h, 804h, 806h (the rest of the 8-byte
 *                 block the address names: the address register is in 8-byte units) one a sample: does one of them fire it?
 * Read together: irq ~0 = the 800h write fired; ~768 / 1,536 / 2,304 = a later halfword of the block did; ~393,216 = the pass missed.
 *
 * WHAT IT DOES. Interrupts off, sound muted, transfer control 0004h (the capture ring writes are IRQ-checked only with bits 2/3).
 * SYNC: the IRQ at 800h on, I_STAT bit 9 polled in a tight loop - T0, the ring's write of 800h (I_STAT is raised at once); every
 * later write of 800h comes 512 samples (393,216 cycles) on. A trial: the IRQ off and acknowledged; wait to T = T0 + k x 393,216
 * - D; write the IRQ address, then SPUCNT with bit 6 (stamp A just before the SPUCNT write); then 1,600 turns of a loop that only
 * reads Timer 2 and I_STAT into arrays (22 cycles a turn), then poll on to 450,000 cycles. Lines:
 *   C trial D A irq       D asked (cycles before the write; negative = after it); A: the enable's stamp - (T0 + k x 393,216);
 *                         irq: I_STAT bit 9's first set read - (T0 + k x 393,216), -1 none in 450,000 cycles. Every wait is
 * bounded. No assertions. All stamps are one unwrapped Timer 2 count (system clock).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define SPU(o) (0x1F801C00u + (o))
#define RING 393216u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }

static uint16_t cnt;
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}
static void irqOff() {
    cnt = (uint16_t)(cnt & ~0x0040); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0040); i++) { now(); }
    write32(I_STAT, ~0x200u);
}
#define TURNS 1600
static uint16_t tt[TURNS], ss[TURNS];

/* the first I_STAT bit 9 set from here on, as an unwrapped stamp; 0xFFFFFFFF none within `limit` cycles */
static uint32_t catchIrq(uint32_t t0, uint32_t limit) {
    for (int i = 0; i < TURNS; i++) { tt[i] = read16(T2_COUNT); ss[i] = (uint16_t)read32(I_STAT); }
    uint32_t hit = 0xFFFFFFFFu;
    for (int i = 0; i < TURNS; i++) { const uint32_t t = stamp(tt[i]); if (hit == 0xFFFFFFFFu && (ss[i] & 0x200)) hit = t; }
    while (hit == 0xFFFFFFFFu) { const uint32_t t = now(); if (read32(I_STAT) & 0x200) { hit = t; break; } if (t - t0 > limit) break; }
    return hit;
}

static void run(int trials, int32_t dLo, int32_t dHi) {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);                                    /* the reverb's work area out of the way */
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write16(SPU(0x1AC), 0x0004);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    { const uint32_t t0 = now(); while (now() - t0 < 100000u) {} }
    irqOff();
    write16(SPU(0x1A4), 0x800 / 8);
    cnt = (uint16_t)(cnt | 0x0040); write16(SPU(0x1AA), cnt);
    uint32_t T0 = 0;
    { const uint32_t ts = now(); for (;;) { const uint16_t t = read16(T2_COUNT); const uint32_t s = read32(I_STAT); const uint32_t x = stamp(t); if (s & 0x200) { T0 = x; break; } if (x - ts > 900000u) { printf("S timeout\n"); break; } } }
    irqOff();
    printf("S %u\n", (unsigned)T0);
    uint32_t base = T0 + RING;
    for (int id = 0; id < trials; id++) {
        const int32_t D = dLo + (int32_t)rnd((uint32_t)(dHi - dLo)); const uint32_t f = rnd(48);
        irqOff();
#ifdef ADDRESS_MOVE
        write16(SPU(0x1A4), 0x900 / 8);                              /* the IRQ on, at an address the ring reaches 128 samples on */
        cnt = (uint16_t)(cnt | 0x0040); write16(SPU(0x1AA), cnt);
#endif
        while ((int32_t)(base - D - now()) < 4000) base += RING;   /* the next pass with room to wait */
        while ((int32_t)(base - D - now()) > 0) {}
        fine(f);
        const uint16_t ta = read16(T2_COUNT);
#ifdef ADDRESS_MOVE
        write16(SPU(0x1A4), 0x800 / 8);                              /* the address moved onto the write about to come */
#else
        cnt = (uint16_t)(cnt | 0x0040); write16(SPU(0x1AA), cnt);
#endif
        const uint32_t A = stamp(ta);
        const uint32_t hit = catchIrq(A, 450000u);
        now(); printf("C %d %d %d", id, (int)D, (int)(A - base));
        now(); printf(" %d\n", hit == 0xFFFFFFFFu ? -1 : (int)(hit - base)); now();
        base += RING;
    }
    irqOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    write16(T2_MODE, m2);
    ExitCriticalSection();
}
