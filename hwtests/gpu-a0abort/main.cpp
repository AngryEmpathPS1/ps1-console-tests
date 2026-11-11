/*
 * gpu/gpu-a0abort - a RAM-to-VRAM transfer cut after N words at a line start: where the upload intake stood, word by word, for a
 * burst, chopped bursts and SyncMode 1 blocks (2026-11-11)
 *
 * WHY. hwtests/gpu-burstrow / gpu-burstrow2 on the second SCPH-5501 give the upload intake as a COUNT (35 words ahead of what is
 * written to VRAM) and, at a line's start, two long holds of the VRAM with exactly one 16-word unit written between them (a burst
 * loses a lap of 32 at each, a unit apart). A burst chopped 16 words + 1 cycle fits the same two holds. But SyncMode 1 blocks of 16
 * to 32 words take the time of the FIRST hold only (hwtests/gpu-dma-quiet, gpu-dma-progress2, dma/chopping as that console ran it;
 * STATUS "The count as a knob core"): with the request "FIFO empty" they cannot ride both. Those tests time a whole upload. This one
 * looks inside: the transfer's length is N words and nothing more - when the DMA ends, GP0's FIFO holds whatever the intake had not
 * taken - then GPUSTAT is read, GP1(01h) ends the transfer, and the rectangle is read back. The words found are the words the GPU
 * TOOK (or, if GP1(01h) throws its buffer away, the words it had WRITTEN: the control rows say which). N stepped through the line
 * start gives the intake's place against time for each feeder; where it stands still, for how many words, is the hold.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, the display as the runner leaves it. A rectangle 256 x 8 at
 * (0,256), filled with zeros first; word i's two pixels are i + 1. A GPUSTAT bit-31 change (a line edge), d cycles, then DMA2:
 *   K 0  one burst (SyncMode 0) of N words                      d 1700, N 300..700 by 4; d 1000 (a refill), N 380..620 by 4
 *   K 1  chopped, 16 words then 1 cycle, N words                d 1700, N 280..680 by 4
 *   K 2  chopped, 16 words then 4 cycles, N words               d 1700, N 220..620 by 4
 *   K 3  SyncMode 1 blocks of 16, N words (N / 16 blocks)       d 1700..1724 by 4, N 160..480 by 16
 *   K 4  SyncMode 1 blocks of 32, N words                       d 1700..1740 by 4, N 160..640 by 32
 *   K 5  the control: one burst, d 200 (no fetch near its end)  N 96..176 by 16, three times
 * CHCR2 is polled until the transfer ends (Timer 2 around it), GPUSTAT read at once (s1), GP1(01h) written, GPUSTAT read again (s2);
 * 3,000 cycles; GP1(04h) = 0; the rectangle read back through GP0(C0h).
 *
 * READ IT LIKE THIS. R k d n t s1 s2 fill pre cnt top: kind, start, words sent, the transfer's cycles, GPUSTAT's bits 28-24 before
 * and after GP1(01h) (hex; bit 28 = 10h ready for a block, 26 = 04h ready for a command, 25 = 02h the DMA request), fill the
 * rectangle's leading words that are not zero - the words the GPU stored, in order, whatever it lost on the way - pre the leading
 * words in place (word i holding i + 1), cnt every word in place, top the highest index found + 1. With nothing held back fill = n.
 * K 5 says what GP1(01h) does to what the GPU had taken and not yet written: fill = n if it is kept, n less 13..29 if dropped.
 * In K 0-2, fill standing still while n grows is the intake stopped (or the writer, if K 5 says dropped), and for how many words.
 * In K 3-4 a fill under n is a block the intake had not finished when the DMA ended, and t against its neighbours the DMA's wait.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0abort --frames=8000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

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
    printf("R %d %u %d %u %02x %02x %d %d %d %d\n", k, (unsigned)d, n, (unsigned)t, (unsigned)((s1 >> 24) & 0x1F), (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort\n");
    printf("R k d n t s1 s2 fill pre cnt top: a transfer of n words into 256 x 8 cut at its end by GP1(01h), read back. k 0 burst, 1 chopped 16 + 1,\n");
    printf("2 chopped 16 + 4, 3 SyncMode 1 blocks of 16, 4 of 32, 5 the control (a burst away from the scan-out's fetches). No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int r = 0; r < 3; r++) for (int n = 96; n <= 176; n += 16) one(5, 200, n);
    for (int n = 300; n <= 700; n += 4) one(0, 1700, n);
    for (int n = 380; n <= 620; n += 4) one(0, 1000, n);
    for (int n = 280; n <= 680; n += 4) one(1, 1700, n);
    for (int n = 220; n <= 620; n += 4) one(2, 1700, n);
    for (uint32_t d = 1700; d <= 1724; d += 4) for (int n = 160; n <= 480; n += 16) one(3, d, n);
    for (uint32_t d = 1700; d <= 1740; d += 4) for (int n = 160; n <= 640; n += 32) one(4, d, n);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort: Done.\n");
    for (;;) VSync(0);
}
