/*
 * gpu/gpu-a0abort8 - gpu-a0abort3's cut at the scan-out's REFILLS: where five feeders' intakes stand at each of a 512-wide line's
 * three mid-line fetches, on the line's own clock (2026-11-12)
 *
 * WHY. The upload intake's model (GPU_A0) has the line start's two fetches read off hwtests/gpu-a0abort2 - 7 to the cycle (a
 * transfer cut by GP1(01h) at each cycle of the line start: where its fill stands, and where the stand ends). The three REFILLS a
 * 512-wide line has are not measured that way at all: their length (lr, 48 GPU ticks), their place (roff) and whether a part of one
 * is served early in the port's idle cycles (aheadR, 12 cycles) were fitted on a burst's words lost (hwtests/gpu-burstrow2), where
 * the model sits on an edge: a refill stalls its intake ~21 cycles against the 16 words the FIFO takes, so 256-wide bursts lose
 * at 61-78% of the refills they meet where the console loses at 84-95%, and no length puts that right without moving the gaps
 * (48 / 50 / 52 / 54.2 / 56 / 58 ticks: the wide rows' words lost -3.1 / -1.1 / +1.9 / +4.5 / +6.2 / +8.5 a burst, their whole gap
 * lists 460 / 471 / 447 / 376 / 354 / 268 of 1,156). The drawing side of the same console charges a refill 54.2 ticks.
 *
 * WHAT IT DOES. As gpu-a0abort4 (a 1,024 x 2 rectangle at (0,256): rows of 512 words, no row start near; zeros first, word i's
 * pixels i + 1; a GPUSTAT bit-31 change, d cycles, Timer 0 (ph) and Timer 1 (ln) read, DMA2 of n words; CHCR2 polled to its end,
 * GPUSTAT, GP1(01h), GPUSTAT; the rectangle read back), with d and n chosen so the cut x = ph + t sweeps ~170 cycles across each
 * refill r = 1, 2, 3 (this core has them at x ~378-408, 782-812 and 1,186-1,216; the sweeps run x ~330-500, 733-903, 1,137-1,307):
 *   K 0  one burst                       n 200..360 by 2, twice
 *   K 1  chopped, 16 words then 1 cycle  n 180..300 by 2, twice
 *   K 2  chopped, 16 words then 4 cycles n 160..262 by 2
 *   K 3  SyncMode 1 blocks of 16         d over 26 cycles by 2, n 160..288 by 16
 *   K 4  SyncMode 1 blocks of 32         d over 40 cycles by 4, n 160..352 by 32
 * Every transfer starts after the fetch before its refill has ended.
 *
 * READ IT LIKE THIS. R k r d n ph ln t s1 s2 fill pre cnt top: as gpu-a0abort4 with r the refill aimed at. x = ph + t is where the
 * cut fell (cycles after the hblank); fill is what had left the intake's two-word pipe when GP1(01h) came. A feeder's fill standing
 * at 16k + 1 over a span of x is its intake stopped; where the stand begins and ends is the refill's hold on it, and top - cnt the
 * words that never arrived. Rows with ln in the vertical blanking meet no fetch. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-a0abort8 --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MAXW 1024
#define W 1024
#define H 2

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void one(int k, int r, uint32_t d, int n) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)H << 16) | (uint32_t)W;
    const int bs = k == 3 ? 16 : k == 4 ? 32 : 0;
    const uint32_t chcr = k == 1 ? (0x11000101u | (4u << 16) | (0u << 20)) : k == 2 ? (0x11000101u | (4u << 16) | (2u << 20))
                        : bs ? 0x01000201u : 0x11000001u;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, at); write32(GP0, ((uint32_t)H << 16) | 0x3FFu);   /* 3FFh rounds up to 1,024; 400h would be 0 */
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, at); write32(GP0, size);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, bs ? (((uint32_t)(n / bs) << 16) | (uint32_t)bs) : (uint32_t)n);
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
    const uint16_t ph = read16(T0_COUNT);                       /* cycles since the hblank that reset Timer 0: this row's true start */
    const uint16_t ln = read16(T1_COUNT);                       /* hblanks since the vblank: this row's line */
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
    const uint32_t s1 = read32(GP1);
    write32(GP1, 0x01000000u);
    const uint32_t s2 = read32(GP1);
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 200000) write32(CHCR2, 0);
    waitT2(3000);
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int j = 0; j < MAXW; j++) { const uint32_t v = read32(GP0); back[2 * j] = (uint16_t)v; back[2 * j + 1] = (uint16_t)(v >> 16); }
    int pre = 0, cnt = 0, top = 0, fill = 0;
    while (fill < MAXW && back[2 * fill] != 0) fill++;          /* the words the GPU stored: VRAM fills in order, whatever was lost */
    while (pre < MAXW && back[2 * pre] == (uint16_t)(pre + 1)) pre++;
    for (int j = 0; j < MAXW; j++) {
        if (back[2 * j] == (uint16_t)(j + 1)) cnt++;
        if (back[2 * j] > top && back[2 * j] <= MAXW) top = back[2 * j];
    }
    printf("R %d %d %u %d %u %u %u %02x %02x %d %d %d %d\n", k, r, (unsigned)d, n, (unsigned)ph, (unsigned)ln, (unsigned)t, (unsigned)((s1 >> 24) & 0x1F), (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort8\n");
    printf("R k r d n ph ln t s1 s2 fill pre cnt top: a transfer of n words into 1024 x 2 cut at its end by GP1(01h), read back, aimed at the\n");
    printf("line's refill r. k 0 burst, 1 chopped 16 + 1, 2 chopped 16 + 4, 3 SyncMode 1 blocks of 16, 4 of 32; ph = Timer 0, cycles since the last\n");
    printf("hblank, ln = Timer 1, hblanks since the vblank, at the start. No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    const uint16_t m0 = read16(T0_MODE);
    write16(T0_MODE, 0x0003);                                    /* the system clock, reset at each hblank */
    const uint16_t m1 = read16(T1_MODE);
    write16(T1_MODE, 0x0103);                                    /* hblanks, reset at the vblank */
    /* each refill's sweep starts at cut x = X0[r]; d = that less the transfer's cycles at its first n, plus the 301 cycles from
     * Timer 0's reset to bit 31's change */
    static const int X0[3] = { 330, 733, 1137 };
    for (int r = 1; r <= 3; r++) {
        const int x0 = X0[r - 1] + 301;
        for (int p = 0; p < 2; p++) for (int n = 200; n <= 360; n += 2) one(0, r, (uint32_t)(x0 - 235), n);
        for (int p = 0; p < 2; p++) for (int n = 180; n <= 300; n += 2) one(1, r, (uint32_t)(x0 - 261), n);
        for (int n = 160; n <= 262; n += 2) one(2, r, (uint32_t)(x0 - 268), n);
        for (int dd = 0; dd <= 26; dd += 2) for (int n = 160; n <= 288; n += 16) one(3, r, (uint32_t)(x0 - 265 + dd), n);
        for (int dd = 0; dd <= 40; dd += 4) for (int n = 160; n <= 352; n += 32) one(4, r, (uint32_t)(x0 - 212 + dd), n);
    }
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort8: Done.\n");
    for (;;) VSync(0);
}
