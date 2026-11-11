/*
 * gpu/gpu-a0abort4 - gpu-a0abort3 with each row's LINE recorded, the display on and OFF, and the feeders between a burst and
 * chopped 16 + 1 (2026-11-11)
 *
 * WHY. hwtests/gpu-a0abort3 on the second SCPH-5501 (STATUS "The upload intake by width, and gpu-a0abort3 read again"): a burst is
 * held twice at a line start; chopped 16 + 1 (23 cycles a 16 words) is held less and later, slower feeders less still. Three things
 * its rows cannot say. (1) About one row in ten stores every word where its neighbours lose 16-32: the vertical blanking, probably -
 * the row's line was not recorded. Here Timer 1 counts hblanks and is reset at the vblank; its count is read beside ph. (2) Whether
 * the holds are the scan-out's fetches or something that goes on with the display off (the SGRAM's refresh, say): the same feeders
 * with GP1(03h) = 1. (3) Where between a burst (17 cycles a 16 words) and chopped 16 + 1 the holds change: DMA windows of 64 and 32
 * words + 1 cycle, 16 + 2, 8 + 1, and chopped 16 + 1 at three starts 8 cycles apart (its unit's phase).
 *
 * WHAT IT DOES. As gpu-a0abort3 (a 1,024 x 2 rectangle at (0,256): rows of 512 words; zeros first, word i's pixels i + 1; a GPUSTAT
 * bit-31 change, d cycles, Timer 0 (ph) and Timer 1 (ln) read, DMA2 of n words; CHCR2 polled to its end, GPUSTAT, GP1(01h),
 * GPUSTAT; the rectangle read back). In the D 1 rows the display is switched off (GP1(03h) = 1) before the fill and on again after
 * the read-back.
 *
 * READ IT LIKE THIS. R k D d n ph ln t s1 s2 fill pre cnt top: as gpu-a0abort3 with D (1 = display off) and ln (hblanks since the
 * vblank at the start). k: 0 burst, 1 chopped 16 + 1, 5 chopped 32 + 1, 7 chopped 8 + 1, 8 chopped 64 + 1, 9 chopped 16 + 2,
 * 3 SyncMode 1 blocks of 16. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0abort4 --frames=8000 --quiet=2000`.
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

static void one(int k, int off, uint32_t d, int n) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)H << 16) | (uint32_t)W;
    const int bs = k == 3 ? 16 : 0;
    const uint32_t chcr = k == 1 ? (0x11000101u | (4u << 16) | (0u << 20)) : k == 5 ? (0x11000101u | (5u << 16) | (0u << 20))
                        : k == 7 ? (0x11000101u | (3u << 16) | (0u << 20)) : k == 8 ? (0x11000101u | (6u << 16) | (0u << 20))
                        : k == 9 ? (0x11000101u | (4u << 16) | (1u << 20)) : bs ? 0x01000201u : 0x11000001u;
    gpuIdle();
    write32(GP1, 0x03000000u | (off ? 1u : 0u));                 /* the display on, or off for this row */
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
    if (off) write32(GP1, 0x03000000u);
    printf("R %d %d %u %d %u %u %u %02x %02x %d %d %d %d\n", k, off, (unsigned)d, n, (unsigned)ph, (unsigned)ln, (unsigned)t, (unsigned)((s1 >> 24) & 0x1F), (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort4\n");
    printf("R k D d n ph ln t s1 s2 fill pre cnt top: a transfer of n words into 1024 x 2 cut at its end by GP1(01h), read back. k 0 burst, 1 chopped\n");
    printf("16 + 1, 5 chopped 32 + 1, 7 chopped 8 + 1, 8 chopped 64 + 1, 9 chopped 16 + 2, 3 SyncMode 1 blocks of 16; D 1 = the display off; ph = Timer 0,\n");
    printf("cycles since the last hblank, ln = Timer 1, hblanks since the vblank, at the start. No assertions.\n\n");
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
    for (int off = 0; off < 2; off++) {
        for (int n = 340; n <= 500; n += 4) one(0, off, 1700, n);
        for (int n = 260; n <= 420; n += 4) one(1, off, 1700, n);
        for (uint32_t d = 1700; d <= 1726; d += 4) for (int n = 192; n <= 400; n += 16) one(3, off, d, n);
    }
    for (uint32_t d = 1708; d <= 1716; d += 8) for (int n = 260; n <= 420; n += 4) one(1, 0, d, n);
    for (int n = 320; n <= 480; n += 4) one(8, 0, 1700, n);
    for (int n = 300; n <= 460; n += 4) one(5, 0, 1700, n);
    for (int n = 260; n <= 420; n += 4) one(9, 0, 1700, n);
    for (int n = 200; n <= 360; n += 4) one(7, 0, 1700, n);
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort4: Done.\n");
    for (;;) VSync(0);
}
