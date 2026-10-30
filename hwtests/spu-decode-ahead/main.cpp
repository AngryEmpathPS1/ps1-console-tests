/*
 * spu/spu-decode-ahead - when a voice's decoder reads each part of its sample, against pitch, from key-on.
 *
 * WHY (2026-10-08). spu-irq's VOICE row on all three consoles: voice 0 on a block at 1000h at pitch 1 (a
 * source sample every ~3.1M cycles), keyed on, the IRQ address set to 1008h (the block's second half) 50 ms later -
 * IRQ9 after 10,886,560 cycles, ~0.37 s after key-on: the decoder read the second half when playback was ~4 source
 * samples in. This core: 1,451,184, ~1 sample in. Its decoder (M248, SPU_DECODE_AHEAD 12) keeps 12 decoded samples
 * ahead of the interpolator's oldest tap and reads a halfword (4 samples) a sample while it has fewer, so the second
 * half (samples 12-15) is read when that tap passes 0 - and its restarted voice starts at source sample 3
 * (SPU_KON_START, fitted at 1x to Redux's silicon rings), one sample short of it. The consoles' 4 fits the same
 * decoder with the position starting at 0 - at 1x the two differ only in what the first outputs interpolate, at low
 * pitch by whole samples. Streamed audio paces itself on IRQ9 at a voice's reads, at whatever pitch it plays.
 *
 * WHAT IT DOES. Every voice parked on a silent looping block (as spu-irq), interrupts off, Timer 2 at the system clock
 * / 8, unwrapped. A silent four-block sample at 1000h (block 0 loop start, block 3 loop end and repeat). For each pitch
 * - 0001h, 0010h, 0040h, 0100h, 0400h, 1000h (1x), 3FFFh - and each target - 1000h (block 0's first half: the first
 * read after key-on), 1008h (its second half), 1010h, 1018h, 1020h, 1028h - voice 0 is keyed off, pointed at 1000h at
 * that pitch, IRQ9 enabled on the target BEFORE the key-on, then keyed on: the cycles from the KON write to IRQ9.
 * Pitch 0001h only for 1000h, 1008h and 1010h (block 1 is ~1.8 s in).
 *
 * READ IT LIKE THIS. Each row: the cycles, and that as source samples at the pitch (768 x 4096 / pitch cycles a
 * source sample) less the key-on delay (the 1000h row's cycles at that pitch). A decoder that keeps T samples decoded
 * ahead of a position starting at P reads samples 12-15 (1008h) when the position passes 12 - T + P..., 28-31 (1010h)
 * 16 samples later, and so on: the rows at low pitch give P and T in whole samples; 1x and 3FFFh what is left of them
 * at the speed games mostly play.
 *
 * ON THIS CORE AT v0.377.42 (`--frames=6000 --quiet=6000`): the first read 5,600-6,100 cycles after the KON write at
 * low pitch (the key-on's ~7 samples at 768); then, in source samples after it, 1008h 1.00, 1010h 17.00, 1018h 29.0,
 * 1020h 45.0, 1028h 57.0 at every pitch from 0001h to 0400h - the second half when the oldest tap leaves sample 0, each
 * later half-block 12 or 16 samples on (its 12 and 16 samples); at 1x 5.0 / 18.4 / 31.3 / 47.3 / 59.3, at 3FFFh 14.5 /
 * 49 / 61 / 77 / 89. spu-irq's console row puts 1008h at ~4 at pitch 1.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-decode-ahead --frames=6000 --quiet=6000`.
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

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) { }
}
static void mode(int m) { setCnt((cnt & ~0x30) | (m << 4)); }
static void irqOn(bool on) { setCnt(on ? (cnt | 0x40) : (cnt & ~0x40)); }
static bool irqFlag() { return (r16(SPUSTAT) & 0x40) != 0; }

static void manualWrite(uint32_t a, const uint16_t* d, int n) {
    mode(0);
    w16(0x1A6, a / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, d[i]);
    mode(1);
    uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) { }
    mode(0);
}

/* One key-on: voice 0 at 1000h, `pitch`, IRQ9 on `target` before the KON write; cycles to IRQ9, or -1. */
static int32_t one(uint16_t pitch, uint32_t target, uint32_t limit) {
    irqOn(false);
    /* Back onto the parked block first: a voice keyed off keeps reading where it was (its first version fired at once
     * on whatever half-block the last trial's voice had reached). */
    w16(0x06, 0x1100 / 8); w16(0x0E, 0x1100 / 8); w16(0x04, 0x1000);
    w16(0x188, 0x0001);
    waitCycles(100000);
    w16(0x06, 0x1000 / 8); w16(0x0E, 0x1000 / 8); w16(0x04, pitch);
    w16(0x1A4, target / 8);
    irqOn(true);
    const uint32_t t0 = now();
    w16(0x188, 0x0001);                                  /* key on */
    for (;;) {
        const uint32_t t = now() - t0;
        if (irqFlag()) { irqOn(false); return (int32_t)t; }
        if (t > limit) { irqOn(false); return -1; }
    }
}

static void print2(uint32_t num, uint32_t den) {
    printf("%4d.%02d", (int)(num / den), (int)((100u * (num % den)) / den));
}

int main() {
    printf("\nspu/spu-decode-ahead\n");
    printf("Voice 0 keyed on at 1000h at seven pitches, IRQ9 on each of its first six half-blocks in turn: cycles from the\n");
    printf("KON write to IRQ9, and that in source samples after the 1000h row. No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t prevT2 = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;

    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    w16(0x180, 0); w16(0x182, 0); w16(0x184, 0); w16(0x186, 0); w16(0x198, 0); w16(0x19A, 0);

    static uint16_t park[8], four[32];
    park[0] = 0x0700;                                    /* a silent block looping onto itself */
    for (int i = 0; i < 32; i++) four[i] = 0;
    four[0] = 0x0400;                                    /* block 0: loop start */
    four[24] = 0x0300;                                   /* block 3: loop end + repeat (to block 0) */
    manualWrite(0x1100, park, 8);
    manualWrite(0x1000, four, 32);
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    waitCycles(200000);
    for (int v = 0; v < 24; v++) {
        const uint32_t b = v * 16;
        w16(b + 0, 0); w16(b + 2, 0); w16(b + 4, 0x1000);
        w16(b + 6, 0x1100 / 8); w16(b + 0xE, 0x1100 / 8);
        w16(b + 8, 0x00FF); w16(b + 0xA, 0x0000);
    }
    w16(0x188, 0xFFFF); w16(0x18A, 0x00FF);
    waitCycles(200000);

    static const uint16_t pitches[7] = { 0x0001, 0x0010, 0x0040, 0x0100, 0x0400, 0x1000, 0x3FFF };
    static const uint32_t targets[6] = { 0x1000, 0x1008, 0x1010, 0x1018, 0x1020, 0x1028 };
    for (int p = 0; p < 7; p++) {
        const uint16_t pitch = pitches[p];
        const uint32_t perSample = (uint32_t)(768u * 4096u / pitch);   /* cycles a source sample */
        printf("pitch %04xh (%d cycles a source sample):\n", pitch, (int)perSample);
        int32_t kon = -1;
        for (int t = 0; t < 6; t++) {
            if (pitch == 0x0001 && t > 2) break;
            const uint32_t limit = pitch == 0x0001 ? 160000000u : 40000000u;
            const int32_t c = one(pitch, targets[t], limit);
            if (t == 0) kon = c;
            printf("  %04xh  ", (unsigned)targets[t]);
            if (c < 0) { printf("no IRQ\n"); continue; }
            printf("%9d cycles", (int)c);
            if (t > 0 && kon >= 0 && c >= kon) { printf("   "); print2((uint32_t)(c - kon), perSample); printf(" samples after the first read"); }
            printf("\n");
        }
    }

    w16(0x18C, 0x0001);
    setCnt(0xC000);
    restoreTimer(2, prevT2);
    ExitCriticalSection();
    printf("\nspu/spu-decode-ahead: Done.\n");
    for (;;) { }
}
