/*
 * gpu/gpu-a0abort2 - gpu-a0abort with each row's start TIMED: how long a burst's intake stands at each of the line start's two
 * holds, in cycles (2026-11-11)
 *
 * WHY. hwtests/gpu-a0abort on the second SCPH-5501 (STATUS "gpu-a0abort on the second SCPH-5501"): the line start's second hold of
 * the VRAM reads about 25 cycles for chopped feeders and SyncMode 1 blocks and 35-38 for a burst - but the burst's figure is read
 * off its gap SIZES (a lap of 32 against 16 in the FIFO's ring), not off a clock. gpu-a0abort's rows could not time it: each row
 * starts on a GPUSTAT bit-31 poll, a few cycles of jitter, and its n stepped by 4. Here Timer 0 runs on the system clock and is
 * reset by every hblank, and each row prints its value at the DMA's start: ph, the row's true place in the line. ph + t is where
 * the cut fell; fill against that, over three passes with n stepped by 2, is the intake's place cycle by cycle - the two stands'
 * lengths for a burst, and the same clock under the SyncMode 1 rows' request window.
 *
 * WHAT IT DOES. As gpu-a0abort (a rectangle 256 x 8 at (0,256), zeros first, word i's pixels i + 1; a GPUSTAT bit-31 change, d
 * cycles, DMA2 of n words; CHCR2 polled to its end, GPUSTAT, GP1(01h), GPUSTAT; the rectangle read back):
 *   K 0  one burst                              d 1700, n 360..540 by 2, three passes
 *   K 1  chopped, 16 words then 1 cycle         d 1700, n 280..440 by 2, two passes
 *   K 3  SyncMode 1 blocks of 16                d 1700..1726 by 2, n 224..384 by 16
 *   K 4  SyncMode 1 blocks of 32                d 1700..1740 by 4, n 256..448 by 32
 *
 * READ IT LIKE THIS. R k d n ph t s1 s2 fill pre cnt top: as gpu-a0abort, with ph = Timer 0 (cycles since the last hblank) read
 * just before CHCR2's write. Sort a kind's rows by ph + t: fill standing still over a span of ph + t is the intake stopped for that
 * many cycles; s1 = 00 with fill = n is the request down over an empty FIFO. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0abort2 --frames=8000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MAXW 1024
#define W 256
#define H 8

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void one(int k, uint32_t d, int n) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)H << 16) | (uint32_t)W;
    const int bs = k == 3 ? 16 : k == 4 ? 32 : 0;
    const uint32_t chcr = k == 1 ? (0x11000101u | (4u << 16) | (0u << 20)) : k == 2 ? (0x11000101u | (4u << 16) | (2u << 20))
                        : bs ? 0x01000201u : 0x11000001u;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, at); write32(GP0, size);
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
    printf("R %d %u %d %u %u %02x %02x %d %d %d %d\n", k, (unsigned)d, n, (unsigned)ph, (unsigned)t, (unsigned)((s1 >> 24) & 0x1F), (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort2\n");
    printf("R k d n ph t s1 s2 fill pre cnt top: a transfer of n words into 256 x 8 cut at its end by GP1(01h), read back. k 0 burst, 1 chopped 16 + 1,\n");
    printf("3 SyncMode 1 blocks of 16, 4 of 32; ph = Timer 0, cycles since the last hblank, at the start. No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    const uint16_t m0 = read16(T0_MODE);
    write16(T0_MODE, 0x0003);                                    /* the system clock, reset at each hblank */
    for (int r = 0; r < 3; r++) for (int n = 360; n <= 540; n += 2) one(0, 1700, n);
    for (int r = 0; r < 2; r++) for (int n = 280; n <= 440; n += 2) one(1, 1700, n);
    for (uint32_t d = 1700; d <= 1726; d += 2) for (int n = 224; n <= 384; n += 16) one(3, d, n);
    for (uint32_t d = 1700; d <= 1740; d += 4) for (int n = 256; n <= 448; n += 32) one(4, d, n);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort2: Done.\n");
    for (;;) VSync(0);
}
