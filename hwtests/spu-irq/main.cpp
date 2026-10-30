/*
 * spu/spu-irq - which sound-RAM accesses raise IRQ9, and does the SPU raise it
 * "late", when the IRQ address is SET onto an address it has already reached?
 *
 * WHY. Four references, four rule sets (ROAD list B):
 * - psx-spx: voices reading ADPCM, the capture buffers, data transfers, and
 *   "Reverb is also triggering interrupts if the IRQ address is located in the
 *   reverb buffer area. Unknown which of the various reverb read(s) and/or
 *   reverb write(s)".
 * - DuckStation: the same, but reverb "TODO: This should check interrupts" -
 *   none; and LATE checks: writing the IRQ address, the transfer address or
 *   SPUCNT's IRQ enable fires IRQ9 at once when the transfer address or a
 *   voice's current block (either 8-byte half) already sits on it.
 * - Mednafen: every RAM read and write checks, reverb's included; writing the
 *   IRQ address, the transfer address or SPUCNT checks the transfer address.
 * - This core: transfers, voice block reads and capture writes, and since M242
 *   the late check, on each voice's LAST READ (its block's first half, or the
 *   second once the decoder has read it); no reverb. Redux: no late checks.
 * Games pace streamed audio with IRQ9 (Metal Gear Solid, Legend of Mana, CTR -
 * STATUS M115), so a missing or extra IRQ is a stall or a skip.
 *
 * WHAT IT DOES. Every voice is parked on a silent looping block at 1100h.
 * IRQ9 is watched as SPUSTAT bit 6 with interrupts off; every wait gives up
 * after its timeout and says so. Times are CPU cycles (Timer2, sysclock/8,
 * unwrapped).
 *   TRANSFER (controls): a manual write of 16 halfwords at 3000h with the IRQ
 *     address inside it, and one past its end.
 *   LATE: the transfer address at 2000h, then the IRQ address set onto it;
 *     the IRQ address first, then the transfer address set onto it; both equal
 *     with IRQ9 disabled, then IRQ9 enabled; and a control with the two apart.
 *   VOICE: voice 0 on a looping block at 1000h at pitch 1 (a block lasts 2.6 s)
 *     keyed on, then after 50 ms the IRQ address set to 1000h, and to 1008h:
 *     at once is a late check; ~2.6 s is the next fetch of the block.
 *   REVERB: a 64 KB work area at 70000h, every reverb address register small,
 *     volumes 0, the IRQ address at 78000h: with reverb enabled (the pointer
 *     sweeps the area in 1.49 s) and with it disabled.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.267 (2026-09-29, `node hwtests/run.js spu-irq
 * --frames=20000`): TRANSFER fires inside the write (40 cycles) and not past
 * its end. LATE: the three rows fire at once (32 cycles), the control not at
 * all - M242's late IRQ. VOICE: 1000h at once (40 cycles); 1008h after
 * 1,451,272, when the voice's decoder reads the block's second half. REVERB: no
 * IRQ, enabled or not.
 * AT v0.236, before M242: no LATE row fired, and VOICE waited for the block's
 * next fetch (76,949,064 cycles for 1000h, 76,948,936 for 1008h - 2.27 s).
 * DuckStation would fire all three LATE rows and both VOICE rows at once (it
 * tests either half of a voice's current block); mednafen the transfer rows and
 * REVERB. So 1008h separates this core from DuckStation.
 * pcsx-redux nugget's tests/spu/spu-irq.c pins silicon's rearm.secondPolls = 1:
 * re-arming on the current block fires at once (NASCAR 2001 needs it,
 * DuckStation 73f6521).
 * THE CONSOLES (SCPH-5501, 7501, 9001, two runs each): LATE 216-520 / 32 (twice 520-528) / 24-768, VOICE 1000h 200-880 -
 * at the next sample or so, rarely at once; VOICE 1008h ~10.9M (the second half read ~4 source samples in: spu-decode-
 * ahead); REVERB on 24.809M (the access at the largest tap, mRAPF2, 1D0h before 78000h), off 50.05M (the same access a
 * lap on - the console printed the row before over the cable for ~280,000 cycles). HERE SINCE v0.377.43-44: 672 / 32 /
 * 32, 376, 1,451,152, REVERB on 24,809,376 and off 4,160 (nothing prints between the rows here: the tap is still on
 * the IRQ's 8 bytes).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void waitCycles(uint32_t c) { uint32_t t0 = now(); while (now() - t0 < c) { } }

static uint16_t cnt;      /* SPUCNT as last written */
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) { }   /* applied (psx-spx) */
}
static void mode(int m) { setCnt((cnt & ~0x30) | (m << 4)); }
static void irqOn(bool on) { setCnt(on ? (cnt | 0x40) : (cnt & ~0x40)); }
static bool irqFlag() { return (r16(SPUSTAT) & 0x40) != 0; }
static void irqAck() { irqOn(false); }

/* Manual write of n halfwords at a (psx-spx's recipe). */
static void manualWrite(uint32_t a, const uint16_t* d, int n) {
    mode(0);
    w16(0x1A6, a / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, d[i]);
    mode(1);
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) { }
    mode(0);
}

/* Wait up to `limit` cycles for IRQ9; returns cycles taken, or -1. */
static int32_t waitIrq(uint32_t limit) {
    uint32_t t0 = now();
    for (;;) {
        uint32_t t = now() - t0;
        if (irqFlag()) return (int32_t)t;
        if (t > limit) return -1;
    }
}
static void report(const char* what, int32_t t) {
    if (t < 0) printf("  %-58s no IRQ\n", what);
    else printf("  %-58s IRQ after %8d cycles\n", what, t);
}

static uint16_t block[8];

int main() {
    printf("\nspu/spu-irq\n");
    printf("Which sound-RAM accesses raise IRQ9, and is it raised late when the address is set onto one?\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t prevT2 = initTimer(2, 2);            /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    write16(0x1F801014 + 0, read16(0x1F801014));       /* leave the SPU delay register as the kernel set it */

    w16(0x1AC, 0x0004);                                 /* transfer control: normal */
    setCnt(0xC000);                                     /* SPU on, unmuted, no reverb, no IRQ, stop */
    w16(0x180, 0); w16(0x182, 0);                       /* main volume 0 */
    w16(0x184, 0); w16(0x186, 0);                       /* reverb volume 0 */
    w16(0x198, 0); w16(0x19A, 0);                       /* no voice to reverb */

    /* A silent block that loops onto itself: shift/filter 0, flags start+end+repeat. */
    for (int i = 0; i < 8; i++) block[i] = 0;
    block[0] = 0x0700;
    manualWrite(0x1100, block, 8);
    manualWrite(0x1000, block, 8);
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);             /* key every voice off */
    waitCycles(200000);
    for (int v = 0; v < 24; v++) {
        const uint32_t b = v * 16;
        w16(b + 0, 0); w16(b + 2, 0);                   /* volume 0 */
        w16(b + 4, 0x1000);                             /* pitch 1.0 */
        w16(b + 6, 0x1100 / 8); w16(b + 0xE, 0x1100 / 8);
        w16(b + 8, 0x00FF); w16(b + 0xA, 0x0000);
    }
    w16(0x188, 0xFFFF); w16(0x18A, 0x00FF);             /* every voice onto the parked block */
    waitCycles(200000);

    printf("TRANSFER (controls)\n");
    static uint16_t data[16];
    for (int i = 0; i < 16; i++) data[i] = 0x1111 * (i & 7);
    w16(0x1A4, 0x3010 / 8); irqOn(true);
    manualWrite(0x3000, data, 16);
    report("manual write 3000h-301Fh, IRQ address 3010h", waitIrq(100000)); irqAck();
    w16(0x1A4, 0x3040 / 8); irqOn(true);
    manualWrite(0x3000, data, 16);
    report("manual write 3000h-301Fh, IRQ address 3040h (past the end)", waitIrq(100000)); irqAck();

    printf("LATE (the address set onto one already reached)\n");
    mode(0);
    w16(0x1A4, 0x2300 / 8); w16(0x1A6, 0x2400 / 8); irqOn(true);
    report("control: IRQ 2300h, transfer 2400h", waitIrq(200000)); irqAck();
    w16(0x1A6, 0x2000 / 8); irqOn(true); w16(0x1A4, 0x2000 / 8);
    report("transfer 2000h, then the IRQ address set to 2000h", waitIrq(200000)); irqAck();
    w16(0x1A4, 0x2100 / 8); irqOn(true); w16(0x1A6, 0x2100 / 8);
    report("IRQ address 2100h, then the transfer address set to 2100h", waitIrq(200000)); irqAck();
    w16(0x1A4, 0x2200 / 8); w16(0x1A6, 0x2200 / 8); irqOn(true);
    report("both 2200h with IRQ9 off, then IRQ9 enabled", waitIrq(200000)); irqAck();

    printf("VOICE (voice 0 on a block at 1000h, pitch 1: a block lasts ~2.6 s)\n");
    for (int k = 0; k < 2; k++) {
        const uint32_t irqA = k ? 0x1008 : 0x1000;
        w16(0x1A4, 0x0800 / 8);                         /* somewhere nothing reads */
        w16(0x18C, 0x0001); waitCycles(100000);
        w16(0x06, 0x1000 / 8); w16(0x0E, 0x1000 / 8); w16(0x04, 0x0001);
        w16(0x188, 0x0001);
        waitCycles(1700000);                            /* 50 ms: the voice is on its block */
        irqOn(true);
        w16(0x1A4, irqA / 8);
        report(k ? "IRQ address set to 1008h (the block's second half)" : "IRQ address set to 1000h (the block's first half)",
               waitIrq(140000000));
        irqAck();
    }
    w16(0x18C, 0x0001); waitCycles(100000);
    w16(0x06, 0x1100 / 8); w16(0x0E, 0x1100 / 8); w16(0x04, 0x1000); w16(0x188, 0x0001);

    printf("REVERB (work area 70000h-7FFFFh, IRQ address 78000h)\n");
    w16(0x1A2, 0x70000 / 8);                            /* mBASE */
    for (int r = 0x1C0; r < 0x200; r += 2) w16(r, 0);   /* every volume and coefficient 0 */
    static const uint16_t addrRegs[] = { 0x1C0, 0x1C2, 0x1D4, 0x1D6, 0x1D8, 0x1DA, 0x1DC, 0x1DE, 0x1E0, 0x1E2,
                                         0x1E4, 0x1E6, 0x1E8, 0x1EA, 0x1EC, 0x1EE, 0x1F0, 0x1F2, 0x1F4, 0x1F6, 0x1F8, 0x1FA };
    for (unsigned i = 0; i < sizeof(addrRegs) / sizeof(addrRegs[0]); i++) w16(addrRegs[i], 0x0010 + i * 2);
    w16(0x1A4, 0x78000 / 8);
    setCnt((cnt | 0x0080) & ~0x40); irqOn(true);
    report("reverb ENABLED (SPUCNT bit 7)", waitIrq(70000000)); irqAck();
    setCnt(cnt & ~0x0080); irqOn(true);
    report("reverb disabled", waitIrq(70000000)); irqAck();

    setCnt(0xC000);
    restoreTimer(2, prevT2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) { }
}
