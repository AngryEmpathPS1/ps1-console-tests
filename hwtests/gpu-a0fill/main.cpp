/*
 * gpu/gpu-a0fill - a GPU upload right behind a fill: does the fill serve the scan-out's fetches, or leave a backlog the upload
 * behind it pays? (2026-11-04, for the SCPH-5501)
 *
 * WHY. A model of the scan-out's fetch as VRAM time taken whenever the GPU's own writes leave it free (experiments/_mkpcs.js, STATUS
 * "IN FLIGHT: the scan-out's fetch in the intake's idle time") fits seven transfer measures on the second SCPH-5501 and reads
 * dma/chopping's SyncMode 1 rows with the GPU BUSY 1-3% fast: it counts a fill's time as serving the fetch, so the upload behind it
 * meets no backlog. This times the same upload alone and behind fills of known size, in the display time and in vblank.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks, the display as the runner left it
 * (Unirom's 512x240). A start: 40 lines below vblank's start (or vblank's first hblank edge) and `phase` cycles more (0, 540, 1,080,
 * 1,620). FILL: GP0(02h) of w x h at (0,300) - 64x16, 256x32, 512x64, 960x128 - then GPUSTAT bit 26 polled: the fill's own
 * cycles. UPLOAD: 2,048 words (16-word SyncMode 1 blocks, GP1(04h)=2) into GP0(A0h) at (0,256), 1,024 wide, alone and with the same
 * fill written just before CHCR2's store (its A0h header queued behind the fill): the cycles from the store to the poll's done, to
 * 32 bits. Twice each.
 *
 * READ IT LIKE THIS. F kind size vb phase rep t: kind 'f' the fill alone, 'u' the upload alone (size 0), 'b' the upload behind the
 * fill. The backlog, if any: b - f - u, in the display time against vblank. No assertions - compare against your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
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
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void toPlace(int vb, uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    if (!vb) while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    if (phase) waitT2(phase);
}

static const uint16_t FW[5] = { 0, 64, 256, 512, 960 }, FH[5] = { 0, 16, 32, 64, 128 };   /* a fill is at most 3F0h wide: 1,024 wraps to none */

/* kind 'f': the fill alone; 'u': the upload alone; 'b': the upload behind the fill */
static void one(char kind, int size, int vb, uint32_t phase, int rep) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    if (kind != 'f') {
        write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
        write32(BCR2, (128u << 16) | 16u);
    }
    toPlace(vb, phase);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    if (kind != 'u') { write32(GP0, 0x02101010u); write32(GP0, (300u << 16) | 0u); write32(GP0, ((uint32_t)FH[size] << 16) | (uint32_t)FW[size]); }
    if (kind == 'f') {
        while (!(read32(GP1) & (1u << 26)) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    } else {
        write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (2u << 16) | 1024u);
        write32(CHCR2, 0x01000201u);
        while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    for (int i = 0; i < 8192 && !(read32(GP1) & (1u << 26)); i++) { write32(GP0, 0); for (volatile int k = 0; k < 8; k++) {} }
    write32(GP1, 0x04000000u);
    printf("F %c %d %d %u %d %u\n", kind, kind == 'u' ? 0 : FW[size] * FH[size], vb, (unsigned)phase, rep, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0fill\n");
    printf("F kind size vb phase rep t (f a fill alone, u 2,048 words by 16-word blocks alone, b the upload behind the fill; size in pixels). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const uint32_t PH[4] = { 0, 540, 1080, 1620 };
    for (int vb = 0; vb < 2; vb++) for (int p = 0; p < 4; p++) {
        for (int r = 0; r < 2; r++) one('u', 0, vb, PH[p], r);
        for (int s = 1; s < 5; s++) for (int r = 0; r < 2; r++) { one('f', s, vb, PH[p], r); one('b', s, vb, PH[p], r); }
    }
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0fill: Done.\n");
    for (;;) VSync(0);
}
