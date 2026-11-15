/*
 * gpu/gpu-a0ff9 - FF9's MOVIE COLUMN UPLOAD ALONE: GP0(A0h) 24 x 224 by DMA2, no MDEC (2026-11-15; the console fuzzer session, for the
 * GPU timing session).
 *
 * WHY. hwtests/mdec-replay (FF9's command with every store around it) read each column's upload - GP1(04h) 0, GP0(01h), GP0(A0h) at
 * (24k, 0), 24 x 224 halfwords, GP1(04h) 2, DMA2 SyncMode 1, 168 blocks of 16 words - at 5,745-5,844 cycles on six consoles and
 * 4,674-4,796 on v0.377.355, with DMA0 feeding the MDEC beside it. Alone, the same upload says whether that is DMA0 / DMA2 arbitration
 * or the intake's pace for a 24-halfword-wide upload (12 words a row: a row boundary every 0.75 block).
 *
 * WHAT IT DOES. Under FF9's display (GP1(08h) 11h: 320 wide, 24-bit; ranges 6C58258h / 703E018h; start 0), the upload as FF9 sends it,
 * the words a counting pattern, timed from the CHCR store to bit 24 clear (a tight poll on Timer 2). Rows: in the vertical blanking
 * (the VBlank flag, then 2,000 + 271 j cycles) and in the active display (the flag, then 120,000 + 271 j: ~55 lines in, mid-picture),
 * each as 168 x 16 and as 42 x 64 blocks; eight start points j each, min..max. GP1(04h) and the A0h header outside the timed span.
 *   U <where> bs <16|64> len <min>..<max>
 * No assertions. THE RULE: run it here first: `node tools/hwrunner.js gpu-a0ff9 --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define T2 R16(0x1F801120u)
#define I_STAT R32(0x1F801070u)
#define GP0 R32(0x1F801810u)
#define GP1 R32(0x1F801814u)

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = T2; clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((GP1 & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t* const BUF = (uint32_t*)0x80040000u;

static uint32_t upload(uint32_t wait, int bs) {
    waitReady();
    I_STAT = ~1u;
    { const uint32_t t0 = now(); while (!(I_STAT & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < wait) {} }
    GP1 = 0x04000000u; GP0 = 0x01000000u;
    GP0 = 0xA0000000u; GP0 = 24u; GP0 = (224u << 16) | 24u;
    GP1 = 0x04000002u;
    R32(0x1F8010A0u) = (uint32_t)BUF & 0xFFFFFFu;
    R32(0x1F8010A4u) = bs == 16 ? 0x00A80010u : 0x002A0040u;
    volatile uint32_t* const ch = (volatile uint32_t*)0x1F8010A8u;
    uint16_t l = T2; uint32_t c = 0;
    *ch = 0x01000201u;
    for (uint32_t g = 0; g < 2000000u && (*ch & 0x01000000u); g++) { const uint16_t x = T2; c += (uint16_t)(x - l); l = x; }
    { const uint16_t x = T2; c += (uint16_t)(x - l); }
    GP1 = 0x04000000u;
    return c;
}

int main() {
    printf("\ngpu/gpu-a0ff9\nFF9's column upload alone (A0h 24 x 224 by DMA2): CHCR store to bit 24 clear, blanking and active, 16- and 64-word blocks.\n\n");
    for (uint32_t i = 0; i < 2688; i++) BUF[i] = (i * 0x00010001u) ^ 0x7C1F03E0u;
    EnterCriticalSection();
    const uint16_t m2 = R16(0x1F801124u);
    const uint32_t dpcr0 = R32(0x1F8010F0u);
    R16(0x1F801124u) = 0;
    lastT = T2; clk = 0;
    R32(0x1F8010F0u) = dpcr0 | 0x00000800u;
    GP1 = 0x05000000u; GP1 = 0x06C58258u; GP1 = 0x0703E018u; GP1 = 0x08000011u; GP1 = 0x03000000u;
    GP0 = 0xE6000000u;
    upload(2000, 16); upload(2000, 64);   /* the code warm */
    static const char* where[2] = { "blank", "active" };
    for (int w = 0; w < 2; w++)
        for (int b = 0; b < 2; b++) {
            const int bs = b ? 64 : 16;
            uint32_t lo = 0xFFFFFFFFu, hi = 0;
            for (uint32_t j = 0; j < 8; j++) { const uint32_t t = upload((w ? 120000u : 2000u) + 271u * j, bs); lo = t < lo ? t : lo; hi = t > hi ? t : hi; }
            ExitCriticalSection();
            printf("U %s bs %d len %u..%u\n", where[w], bs, (unsigned)lo, (unsigned)hi);
            EnterCriticalSection();
        }
    R32(0x1F8010F0u) = dpcr0;
    R16(0x1F801124u) = m2;
    ExitCriticalSection();
    printf("\ngpu/gpu-a0ff9: Done.\n");
    for (;;) VSync(0);
}
