/*
 * spu/spu-envrows - hwtests/spu-regs' ENVELOPE rows with NOTHING PRINTED until every row is measured (2026-11-23, the SPU session)
 *
 * WHY. spu-regs keys voice 0 on for each envelope setting and prints the row before the next. Each row ends when ENVX reaches
 * its last level - a moment locked to the SPU's two-sample key-on grid - so the next row's key-on write falls a fixed time
 * after it: the release wait, the set-up, and the PRINT of the row over the serial line (~60 characters, ~180,000 cycles).
 * Where that write falls in the key-on grid decides the next row by a sample or two, so the rows compare a console's serial
 * timing with the emulator's as much as their SPUs. On six consoles (2026-11-23) its linear rate 10 row reads 7 9 14 23 32 36 41
 * on every one and 8 10 15 24 33 37 42 here at every draw tried, while hwtests/spu-konenv (the envelope's first steps against a
 * key-on at a known place) reads the same on the six and here. This is the same measurement with the print taken out of the
 * chain: every row's numbers are kept and printed at the end.
 *
 * WHAT IT DOES. spu-regs' set-up and its envRun, unchanged but for the row's printf: attack linear and exponential at rates
 * 00h-38h, decay at shifts 0-12, sustain down linear and exponential at 20h / 30h / 40h, release linear and exponential at
 * shifts 0-16. One line a row at the end: "R n a b c d e f g" (the samples from the key-on write at which ENVX first reached
 * each level, as spu-regs; -1 not reached, -2 no such column). There are no assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-envrows --frames=9000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt = 0xC000;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    /* An upload's last FIFO-full is still on its way to sound RAM when DMA4 reports done: Stop at once lands 4-5 of 32
     * halfwords (hwtests/spu-dma TAIL on all three consoles, v0.372's model too). Until 2026-10-08 this stopped at once,
     * and the REPEAT blocks past block 3 never arrived - on the consoles, and here since M372 (B and C read 0000) - so
     * every REPEAT row of console day is of whatever sound RAM held there. spu-dma's fix: a settle first. */
    if (toSpu) idle(100000);
    spuMode(0);
}

/* Blocks at 1000h: 0 DC 1000h one-shot (end+repeat), 1 DC 3000h loop (start+end+repeat), 2 and 3 DC 1000h plain,
 * 4 DC 2000h loop start, 5 DC 2000h end+repeat, 6 DC 4000h loop (start+end+repeat) for the envelope. */
uint32_t blocks[32] __attribute__((aligned(16)));
static void block(int n, uint8_t flags, uint8_t nibble) {
    uint8_t* b = (uint8_t*)blocks + 16 * n;
    b[0] = 0x00; b[1] = flags;
    for (int i = 2; i < 16; i++) b[i] = (uint8_t)(nibble | (nibble << 4));
}
#define BLK(n) ((0x1000 + 16 * (n)) / 8)

uint32_t cap[256] __attribute__((aligned(16)));                 /* the voice-1 capture, 800h..BFFh */

static void voiceSetup(int v, uint16_t start, uint16_t adsrLo, uint16_t adsrHi) {
    const uint32_t o = 0x10 * (uint32_t)v;
    write16(SPU(o + 0x0), 0x3FFF); write16(SPU(o + 0x2), 0x3FFF);
    write16(SPU(o + 0x4), 0x1000);
    write16(SPU(o + 0x6), start);
    write16(SPU(o + 0x8), adsrLo); write16(SPU(o + 0xA), adsrHi);
}
static void keyOff(uint32_t mask) { write16(SPU(0x18C), (uint16_t)mask); write16(SPU(0x18E), (uint16_t)(mask >> 16)); }
static void keyOn(uint32_t mask) { write16(SPU(0x188), (uint16_t)mask); write16(SPU(0x18A), (uint16_t)(mask >> 16)); }
static void silence(uint32_t mask) {                            /* key off, and wait out a fast release */
    for (int v = 0; v < 24; v++) if (mask & (1u << v)) write16(SPU(0x10 * v + 0xA), 0x0000);
    keyOff(mask); idle(400000);
}

/* The captured level: the median-ish of 16 samples from the voice-1 capture's first half. */
static int16_t captured() {
    spuDma(0x800, cap, 256, false);
    const int16_t* h = (const int16_t*)cap;
    int16_t v[16];
    for (int i = 0; i < 16; i++) v[i] = h[16 + i * 24];
    for (int i = 1; i < 16; i++) for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int16_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[8];
}

/* ENVELOPE: key on, read ENVX every 256 cycles for up to `limit` cycles; the sample (from the key-on write) at which
 * it first reached each threshold. `falling` looks downward; `offAt` > 0 keys off after that many cycles. */
static const uint16_t UP[7] = { 0x0800, 0x1000, 0x2000, 0x4000, 0x6000, 0x7000, 0x7FFF };
static const uint16_t DOWN[6] = { 0x6000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0000 };
static int32_t res[64][7]; static int nres;
static void envRun(uint16_t lo, uint16_t hi, bool falling, uint32_t offAt, uint32_t limit) {
    silence(1);
    voiceSetup(0, BLK(6), lo, hi);
    const int nt = falling ? 6 : 7;
    const uint16_t* th = falling ? DOWN : UP;
    int32_t at[7]; for (int i = 0; i < 7; i++) at[i] = -1;
    int k = 0;
    bool off = false;
    uint16_t peak = 0;
    const uint32_t t0 = now();
    keyOn(1);
    for (;;) {
        const uint32_t t = now() - t0;
        if (t > limit || k >= nt) break;
        if (offAt && !off && t >= offAt) { keyOff(1); off = true; }
        const uint16_t e = read16(SPU(0x00C));
        if (e > peak) peak = e;
        if (falling ? (off || !offAt) && (int16_t)e <= (int16_t)th[k] && peak >= 0x7000 : e >= th[k]) { at[k] = (int32_t)(t / 768); k++; }
        while (now() - t0 < t + 256) {}
    }
    for (int i = 0; i < 7; i++) res[nres][i] = i < nt ? at[i] : -2;
    nres++;
}

int main() {
    printf("\nspu/spu-envrows\n");
    printf("spu-regs' envelope rows, printed after all are measured: R n and the seven columns. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    keyOff(0xFFFFFF); idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    for (int i = 0; i < 32; i++) blocks[i] = 0;
    block(0, 0x03, 1); block(1, 0x07, 3); block(2, 0x00, 1); block(3, 0x00, 1); block(4, 0x04, 2); block(5, 0x03, 2);
    block(6, 0x07, 4);
    spuDma(0x1000, blocks, 32, true);
    silence(2);

    nres = 0;
    static const uint8_t AR[] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x2C, 0x30, 0x38 };
    for (int e = 0; e < 2; e++)
        for (unsigned i = 0; i < sizeof(AR); i++)
            envRun((uint16_t)((e ? 0x8000 : 0) | (AR[i] << 8) | 0x000F), 0x0000, false, 0, 33868800u / 2);
    for (int d = 0; d <= 12; d += 2) envRun((uint16_t)((d << 4) | 0x0000), 0x5FC0, true, 0, 33868800u / 2);
    static const uint8_t SR[] = { 0x20, 0x30, 0x40 };
    for (unsigned i = 0; i < sizeof(SR); i++) {
        envRun(0x000F, (uint16_t)(0x4000 | (SR[i] << 6)), true, 0, 33868800u / 2);
        envRun(0x000F, (uint16_t)(0xC000 | (SR[i] << 6)), true, 0, 33868800u / 2);
    }
    static const uint8_t RR[] = { 0, 4, 8, 12, 16 };
    for (unsigned i = 0; i < sizeof(RR); i++) {
        envRun(0x000F, (uint16_t)(RR[i]), true, 768 * 40, 33868800u / 2);
        envRun(0x000F, (uint16_t)(0x0020 | RR[i]), true, 768 * 40, 33868800u / 2);
    }
    silence(1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int n = 0; n < nres; n++) {
        printf("R %2d", n);
        for (int i = 0; i < 7; i++) printf(" %d", (int)res[n][i]);
        printf("\n");
    }
    printf("\nspu/spu-envrows: Done.\n");
    for (;;) VSync(0);
}
