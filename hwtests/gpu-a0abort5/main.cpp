/*
 * gpu/gpu-a0abort5 - on which of the frame's lines is an upload held at the line start? A burst and chopped 16 + 1 started on
 * every second line of the frame, the display on and off (2026-11-11)
 *
 * WHY. hwtests/gpu-a0abort4 on the second SCPH-5501 and the PSone (STATUS "gpu-a0abort4 on the second SCPH-5501 and the PSone"): a
 * burst across a line start loses the same words with the display off, and on rows that start 0-3 hblanks after the vblank's
 * reset of Timer 1 as on rows mid-frame - where this emulator holds the VRAM for the scan-out on visible lines only. That test
 * started at four places of the frame. Here the start is CHOSEN: the row waits for Timer 1 (hblanks since the vblank) to read L,
 * then as before.
 *
 * WHAT IT DOES. As gpu-a0abort4 (1,024 x 2 at (0,256), rows of 512 words; a GPUSTAT bit-31 change, d = 1,700 cycles, Timer 0 (ph)
 * and Timer 1 (ln) read, DMA2 of n words; CHCR2 polled to its end, GPUSTAT, GP1(01h), GPUSTAT; the rectangle read back), after a
 * wait for Timer 1 = L: a burst of 480 words for L = 0, 2 .. 262 with the display on, L = 0, 4 .. 260 with it off; chopped 16 + 1
 * of 400 words for L = 0, 4 .. 260 with the display on.
 *
 * READ IT LIKE THIS. R k D L n ph ln t s1 s2 fill pre cnt top: as gpu-a0abort4 with L the line asked for and ln the line read at
 * the start (L, or L + 1). top - cnt = the words missing. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0abort5 --frames=8000 --quiet=2000`.
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

static void one(int k, int off, int L, int n) {
    const uint32_t d = 1700;
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
    for (int i = 0; i < 400000 && read16(T1_COUNT) != (uint16_t)L; i++) {}   /* the line asked for (bounded: two frames) */
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
    printf("R %d %d %u %d %u %u %u %02x %02x %d %d %d %d\n", k, off, (unsigned)L, n, (unsigned)ph, (unsigned)ln, (unsigned)t, (unsigned)((s1 >> 24) & 0x1F), (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort5\n");
    printf("R k D L n ph ln t s1 s2 fill pre cnt top: a transfer of n words into 1024 x 2 started on line L (Timer 1, hblanks since the vblank),\n");
    printf("cut at its end by GP1(01h), read back. k 0 burst, 1 chopped 16 + 1; D 1 = the display off; top - cnt = the words missing. No assertions.\n\n");
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
    for (int L = 0; L <= 262; L += 2) one(0, 0, L, 480);
    for (int L = 0; L <= 260; L += 4) one(0, 1, L, 480);
    for (int L = 0; L <= 260; L += 4) one(1, 0, L, 400);
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort5: Done.\n");
    for (;;) VSync(0);
}
