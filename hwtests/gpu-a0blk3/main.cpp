/*
 * gpu/gpu-a0blk3 - does a CHOPPED or a BURST DMA2 transfer slow the SyncMode 1 uploads after it? (2026-11-04, for the SCPH-5501)
 *
 * WHY. On the second SCPH-5501, 2,048 words of 4-word SyncMode 1 blocks into a 1,024-wide rectangle inside vblank take 7,319-7,330
 * cycles in hwtests/gpu-a0blk and -a0blk2 (whatever the display's writes; this emulator 7,315-7,332) and 7,560-7,576 in
 * hwtests/gpu-a0mode, from its first upload on, every run alike - the same upload and poll. gpu-a0mode and gpu-a0chop are the
 * programs that also run chopped transfers and bursts; the runner clears every channel's CHCR between programs. This runs the same
 * block upload before and after each.
 *
 * WHAT IT DOES. As gpu-a0blk2 (inside vblank, 500 cycles past its first hblank edge, a CHCR2 and Timer 2 poll, the display as the
 * runner left it): phase A four block uploads (2,048 words, 4-word blocks, 1,024 wide at (0,256)); phase B four chopped transfers of
 * the same (4-word DMA windows, 4-cycle CPU windows); phase C four block uploads; phase D four bursts of 1,024 words into a 64-wide
 * rectangle in the display time, 40 lines below vblank (they lose words); phase E four block uploads; phase F CHCR2 written 0 and
 * GP1(00h), then four block uploads.
 *
 * READ IT LIKE THIS. Z phase kind rep t left lost. No assertions - compare against your emulator.
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
static uint8_t seen[WORDS + 1];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* vblank's first hblank edge (or 40 lines below it), then 500 cycles */
static void toPlace(int lines) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < (uint16_t)lines) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

/* kind 'S' 4-word blocks, 'P' chopped 4/4, 'B' a burst of 1,024 words into 64 wide, 40 lines down */
static void one(char phase, char kind, int rep) {
    const int words = kind == 'B' ? 1024 : WORDS, w = kind == 'B' ? 64 : 1024, h = words * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? ((512u << 16) | 4u) : (uint32_t)words);
    const uint32_t chcr = kind == 'S' ? 0x01000201u : kind == 'P' ? (0x11000101u | (2u << 16) | (2u << 20)) : 0x11000001u;
    toPlace(kind == 'B' ? 40 : 0);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int i = 0; i <= WORDS; i++) seen[i] = 0;
    for (int k = 0; k < words; k++) { const uint32_t v = read32(GP0); const int a = v & 0xFFFF; if (a >= 1 && a <= words) seen[a] = 1; }
    int lost = 0;
    for (int i = 1; i <= words; i++) if (!seen[i]) lost++;
    printf("Z %c %c %d %u %u %d\n", phase, kind, rep, (unsigned)t, (unsigned)left, lost);
}

int main() {
    printf("\ngpu/gpu-a0blk3\n");
    printf("Z phase kind rep t left lost (S 4-word SyncMode 1 blocks in vblank, P chopped 4/4 in vblank, B a burst in the display). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int r = 0; r < 4; r++) one('A', 'S', r);
    for (int r = 0; r < 4; r++) one('B', 'P', r);
    for (int r = 0; r < 4; r++) one('C', 'S', r);
    for (int r = 0; r < 4; r++) one('D', 'B', r);
    for (int r = 0; r < 4; r++) one('E', 'S', r);
    write32(CHCR2, 0); write32(GP1, 0x00000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int r = 0; r < 4; r++) one('F', 'S', r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0blk3: Done.\n");
    for (;;) VSync(0);
}
