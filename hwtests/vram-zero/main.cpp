/*
 * gpu/vram-zero - is VRAM zero when a test starts? (runner v8's clean slate, 2026-10-26)
 *
 * WHY. Runner v8 zeroes VRAM before every test (four 512 x 256 fills; hwtests/runner "A CLEAN SLATE"). This reads all of
 * VRAM back through GP0(C0h) and counts the halfwords that are not 0000h, then fills it with 4040h (gpu-shape-time68c's
 * grey) before it ends: run twice in a row under the runner, the second run reads 0 where the runner cleared and 524,288
 * where it did not.
 *
 * READ IT LIKE THIS. "nonzero halfwords N of 524288" - 0 under runner v8 from the second run on. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define GP0R     (*(volatile uint32_t*)0x1F801810)
#define GPUREAD  (*(volatile uint32_t*)0x1F801810)
#define GP1R     (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT  (*(volatile uint32_t*)0x1F801814)

static void waitIdle() { for (int i = 0; i < 2000000 && !(GPUSTAT & 0x04000000u); i++) {} }

int main() {
    printf("\ngpu/vram-zero\n");
    waitIdle();
    GP0R = 0xC0000000u;                                /* VRAM to the CPU: all of it */
    GP0R = 0;
    GP0R = (512u << 16) | 1024u;
    for (int i = 0; i < 2000000 && !(GPUSTAT & 0x08000000u); i++) {}
    uint32_t nz = 0, first = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < 1024u * 512u / 2u; i++) {
        const uint32_t w = GPUREAD;
        if (w & 0xFFFFu) { nz++; if (first == 0xFFFFFFFFu) first = i * 2; }
        if (w >> 16) { nz++; if (first == 0xFFFFFFFFu) first = i * 2 + 1; }
    }
    printf("nonzero halfwords %u of 524288", (unsigned)nz);
    if (nz) printf(" (the first at x %u, y %u)", (unsigned)(first % 1024u), (unsigned)(first / 1024u));
    printf("\n");
    for (uint32_t n = 0; n < 4; n++) {                 /* 4040h everywhere for the next run to find */
        waitIdle();
        GP0R = 0x02404040u;
        GP0R = ((n >> 1) * 256u << 16) | ((n & 1) * 512u);
        GP0R = (256u << 16) | 512u;
    }
    waitIdle();
    printf("gpu/vram-zero: Done.\n");
    for (;;) {}
}
