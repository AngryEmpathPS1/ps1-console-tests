/*
 * gpu/gpu-a0abort6 - both ends of the vertical blanking: a burst across EVERY line start from Timer 1 = 226 to 12, started on the
 * hblank itself, and Timer 1 against GPUSTAT bit 31 at every hblank of a frame (2026-11-11)
 *
 * WHY. hwtests/gpu-a0abort5 on the second SCPH-5501 and the PSone (STATUS "gpu-a0abort5 on the second SCPH-5501 and the PSone"):
 * a burst loses 93 or 109 words at every displayed line's start and none at the one after Timer 1 = 236, the blanking's first. Its
 * rows start on a GPUSTAT bit-31 change, which does not come in the blanking, so it has no row inside the blanking nor at its
 * end - where the fetch for the first displayed line should be, a line earlier than this emulator has it. And its wait never read
 * Timer 1 = 262 on either console.
 *
 * WHAT IT DOES. T rows first: Timer 1 (hblanks, reset at the vblank) polled over two frames; at each change, its new value, Timer 0
 * (cycles since the hblank) and GPUSTAT bit 31 as read right after. Then R rows as gpu-a0abort5 (1,024 x 2 at (0,256), a burst of
 * 480 words cut at its end by GP1(01h), read back), but started by the timers alone: a wait for Timer 1 to become L, then for
 * Timer 0 (reset ~540 cycles after Timer 1's tick) to pass 1,380 - gpu-a0abort5's start, the burst across Timer 1's tick to L + 1. L = 226 .. 262, 0 .. 12 and 100, three times.
 *
 * READ IT LIKE THIS. T ln ph b: Timer 1's new value, Timer 0 then, bit 31. R k D L n ph ln t s1 s2 fill pre cnt top: as
 * gpu-a0abort5; top - cnt = the words missing. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-a0abort6 --emu`.
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
    const uint32_t d = 1380;
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
    /* Timer 1 becomes L (bounded), then Timer 0 passes d */
    for (int i = 0; i < 400000 && read16(T1_COUNT) == (uint16_t)L; i++) {}   /* not on a line L already under way */
    for (int i = 0; i < 400000 && read16(T1_COUNT) != (uint16_t)L; i++) {}   /* the line asked for (bounded: two frames) */
    for (int i = 0; i < 2000 && read16(T0_COUNT) >= 1000; i++) {}            /* Timer 1 ticks ~1,630 cycles into Timer 0's line: past Timer 0's reset */
    for (int i = 0; i < 2000 && read16(T0_COUNT) < d; i++) {}                /* then d cycles: the start gpu-a0abort5's rows had (ph ~1,400) */
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
    printf("\ngpu/gpu-a0abort6\n");
    printf("T ln ph b: Timer 1 (hblanks, reset at the vblank) at each change over two frames, Timer 0 (cycles since the hblank) and GPUSTAT bit 31 then.\n");
    printf("R k D L n ph ln t s1 s2 fill pre cnt top: a burst of n words into 1024 x 2 started at Timer 0 = 1,380 on line L, cut at its end by GP1(01h),\n");
    printf("read back; top - cnt = the words missing. No assertions.\n\n");
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
    {   /* Timer 1 against bit 31, every hblank of two frames */
        static uint16_t tl[560], tp[560]; static uint8_t tb[560];
        int c = 0; uint16_t last = read16(T1_COUNT);
        for (int i = 0; i < 600000 && c < 560; i++) {
            const uint16_t v = read16(T1_COUNT);
            if (v != last) { tp[c] = read16(T0_COUNT); tb[c] = (uint8_t)(read32(GP1) >> 31); tl[c++] = v; last = v; }
        }
        for (int i = 0; i < c; i++) if (tl[i] >= 224 || tl[i] <= 14 || (i && tl[i] != (uint16_t)(tl[i - 1] + 1)) || tp[i] < 1600 || tp[i] > 1680) printf("T %u %u %u\n", (unsigned)tl[i], (unsigned)tp[i], (unsigned)tb[i]);
    }
    for (int r = 0; r < 3; r++) {
        for (int L = 226; L <= 262; L++) one(0, 0, L, 480);
        for (int L = 0; L <= 12; L++) one(0, 0, L, 480);
        one(0, 0, 100, 480);
    }
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort6: Done.\n");
    for (;;) VSync(0);
}
