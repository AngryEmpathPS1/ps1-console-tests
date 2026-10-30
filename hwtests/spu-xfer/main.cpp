/*
 * spu/spu-xfer - SPUSTAT around sound-RAM transfers: the manual write's busy
 * time, which request bits each DMA mode raises and when, and what each
 * transfer type writes.
 *
 * WHY.
 * - BUSY (psx-spx "SPU RAM Manual Write"): halfwords go into a 32-entry FIFO,
 *   SPUCNT's mode 1 starts the write, bit 10 stays up until it is done, and a
 *   second block in mode 1 needs "some hardcoded delay of at least 300h
 *   cycles". This core writes each halfword straight to RAM and never raises
 *   bit 10 (SPU readStatus). No number for the busy time exists anywhere.
 * - REQUEST BITS (STATUS M228): DMA write raises bit 9 on consoles, not the
 *   bit 8 psx-spx names - Cyberwar (Japan) froze without it. DMA READ's bit is
 *   measured by nothing (bit 8 is the references' pairing), and nocash says
 *   "When switching SPUCNT to DMA-read mode, status bit9 and bit7 aren't set
 *   immediately". Nothing measures how long "not immediately" is.
 * - TYPES (psx-spx 1F801DACh): 0,1,6,7 Fill (the FIFO's last halfword
 *   everywhere), 2 Normal, 3 Rep2, 4 Rep4, 5 Rep8 - nocash's table, "rather
 *   bizarre". Sony's libraries only ever write 0004h (Normal). This core
 *   treats every type as Normal.
 *
 * WHAT IT DOES. Times are CPU cycles (Timer2, sysclock/8, unwrapped).
 *   BUSY: for N = 1, 2, 4, 8, 16, 32 halfwords: Stop, the address, N writes to
 *     the FIFO, then mode 1 - the cycles until SPUSTAT's mode bits show it,
 *     and until bit 10 clears. Then the multi-block way: mode 1 left on, N
 *     more writes, and the cycles until bit 10 clears.
 *   REQUEST: from Stop to DMA write (mode 2) and to DMA read (mode 3): SPUSTAT
 *     read back to back for 2,000 cycles, printed wherever bits 5-0 or 7-10
 *     change.
 *   TYPES: for each type 0-7, the 8 halfwords 1111h..8888h written by hand
 *     at 5000h (over a 16-halfword field of EEEEh), then read back by DMA4.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js spu-xfer`): BUSY -
 * the mode bits show at the first poll (96-128 cycles, the reads' own cost) and
 * bit 10 never rises, at any N, either way. REQUEST: DMA write shows 02A0h and
 * DMA read 01B0h at the first read after the write (112 and 72 cycles). TYPES:
 * every type writes 1111h..8888h as Normal. psx-spx's table and its "not set
 * immediately" are both unmodelled here; a console that follows them has
 * found two.
 * THE CONSOLES (console day, all three): BUSY's mode applied 376-920 cycles
 * after the write (N=2/4 704, N=8 632, N=16 560-640, N=32 424-496), bit 10 set
 * at the apply for N=16 and 32 and clearing ~620 / ~1,430 later; REQUEST shows
 * the mode with bits 9 and 7 in one step (0800h -> 0AA0h on the 9001); TYPES
 * decimate: type 3 every 2nd halfword, 4 every 4th, 5, 0, 1, 6 and 7 one.
 * AT v0.372 (M372: SPUSTAT takes SPUCNT once a sample): applied 744 / 640 / 576
 * / 504 / 304 / 632 - falling ~25 cycles a FIFO write where the consoles' fall
 * ~9 (their stores to the SPU are cheaper: open); bit 10 and the types as before.
 * AT v0.373 (M373: an SPU store waits in the write buffer, 7 cycles of bus): the
 * FIFO loop 9 cycles a turn (the consoles ~9.4); applied 552 / 720 / 720 / 648 /
 * 512 / 432 against the consoles' ~704 / 704 / 632 / 560-640 / 424-496.
 * AT v0.376 (M376: the transfer types decimate a manual write): TYPES as the consoles, every 2nd / 4th / 8th - each
 * row led by FFFFh here, because the read-back takes 1F801014h as it finds it (no write of its own): sideloaded, the
 * BIOS's 200931E1h, M372's unstable read; on the consoles 200931E1h or 220931E1h by what ran before (spu-dma's
 * start line read both).
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

static uint16_t cnt = 0xC000;
static void setMode(int m, bool wait) {
    cnt = (cnt & ~0x30) | (m << 4);
    w16(SPUCNT, cnt);
    if (!wait) return;
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (cnt & 0x3F) && now() - t0 < 100000) { }
}
static uint32_t waitBusy() {
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) { }
    return now() - t0;
}

/* DMA4 SPU -> RAM, `words` 32-bit words in 16-word blocks. */
static void dmaRead(uint32_t spuAddr, uint32_t* dst, int words) {
    setMode(0, true);
    w16(0x1A6, spuAddr / 8);
    setMode(3, true);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x00080000);          /* DMA4 on */
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFF);
    write32(0x1F8010C4, ((words / 16) << 16) | 16);
    write32(0x1F8010C8, 0x01000200);                                /* to RAM, SyncMode 1 */
    uint32_t t0 = now();
    while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 2000000) { }
    setMode(0, true);
}

static uint32_t buf[16];

int main() {
    printf("\nspu/spu-xfer\n");
    printf("SPUSTAT around sound-RAM transfers: busy time, request bits by mode, transfer types.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t prevT2 = initTimer(2, 2);            /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    w16(0x1AC, 0x0004);
    w16(SPUCNT, cnt); setMode(0, true);

    printf("BUSY (manual write: the mode applied / bit 10 clear, cycles after the SPUCNT write)\n");
    static const int ns[] = { 1, 2, 4, 8, 16, 32 };
    for (unsigned k = 0; k < sizeof(ns) / sizeof(ns[0]); k++) {
        const int n = ns[k];
        setMode(0, true);
        w16(0x1A6, 0x4000 / 8);
        for (int i = 0; i < n; i++) w16(0x1A8, 0x1234 + i);
        const uint32_t t0 = now();
        setMode(1, false);
        while ((r16(SPUSTAT) & 0x3F) != (cnt & 0x3F) && now() - t0 < 100000) { }
        const uint32_t ta = now() - t0;
        const uint16_t s = r16(SPUSTAT);
        const uint32_t tb = waitBusy() + (now() - t0) * 0;   /* from the apply */
        /* the multi-block way: mode 1 still on */
        for (int i = 0; i < n; i++) w16(0x1A8, 0x4321 + i);
        const uint16_t s2 = r16(SPUSTAT);
        const uint32_t tc = waitBusy();
        printf("  N=%2d  applied %5d  SPUSTAT then %04x  bit 10 clear %6d later  | mode 1 kept: SPUSTAT %04x, clear after %6d\n",
               n, ta, s, tb, s2, tc);
    }
    setMode(0, true);

    printf("REQUEST (Stop -> mode, SPUSTAT read back to back; each change with its cycle)\n");
    for (int m = 2; m <= 3; m++) {
        setMode(0, true);
        w16(0x1A6, 0x6000 / 8);
        uint16_t last = r16(SPUSTAT);
        printf("  mode %d (%s): start %04x", m, m == 2 ? "DMA write" : "DMA read", last);
        const uint32_t t0 = now();     /* after the printf, which takes ~47,000 cycles */
        w16(SPUCNT, (cnt & ~0x30) | (m << 4));
        int changes = 0;
        for (;;) {
            const uint32_t t = now() - t0;
            const uint16_t s = r16(SPUSTAT);
            if (s != last && changes < 8) { printf("  %d:%04x", t, s); last = s; changes++; }
            if (t > 2000) break;
        }
        cnt = (cnt & ~0x30) | (m << 4);
        printf("\n");
        setMode(0, true);
    }

    printf("TYPES (1F801DACh = type << 1; 1111h..8888h written by hand over EEEEh, read back by DMA4)\n");
    for (int type = 0; type < 8; type++) {
        w16(0x1AC, 0x0004);
        setMode(0, true); w16(0x1A6, 0x5000 / 8);
        for (int i = 0; i < 16; i++) w16(0x1A8, 0xEEEE);
        setMode(1, true); waitBusy(); setMode(0, true);
        w16(0x1AC, type << 1);
        w16(0x1A6, 0x5000 / 8);
        for (int i = 0; i < 8; i++) w16(0x1A8, 0x1111 * (i + 1));
        setMode(1, true); waitBusy(); setMode(0, true);
        w16(0x1AC, 0x0004);
        for (int i = 0; i < 16; i++) buf[i] = 0;
        dmaRead(0x5000, buf, 16);
        const uint16_t* h = (const uint16_t*)buf;
        printf("  type %d:", type);
        for (int i = 0; i < 12; i++) printf(" %04x", h[i]);
        printf("\n");
    }
    w16(0x1AC, 0x0004);

    restoreTimer(2, prevT2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) { }
}
