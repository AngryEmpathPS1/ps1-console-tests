/*
 * gpu/gpu-tcset - GP0(03h) = 0, one word, and nothing else (2026-11-02)
 *
 * WHY. The first SCPH-5501's slow boot of 2026-11-01 (2 cycles a row on plain draws, survived GP1(00h), every test after it) may be
 * GP0(03h) "Timing Control" at a value some test sent by accident: hwtests/gpu-timectl read this console's state after a RESET as
 * value 0 (3 x 128 and 64 x 64 within 3 cycles). If the slow state comes back, this one word, with hwtests/dma-order before and after
 * it, says whether the register holds it: dma-order's 4-word row back to 7,314 from ~7,480 means it does.
 *
 * WHAT IT DOES. Waits for the GPU to be idle (GPUSTAT 26 and 28), writes GP0(03000000h), waits again, prints GPUSTAT before and after.
 *
 * There are no assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tcset`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define GP0 0x1F801810u
#define GP1 0x1F801814u

static void waitIdle() { for (int i = 0; i < 4000000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

int main() {
    printf("\ngpu/gpu-tcset\n");
    EnterCriticalSection();
    waitIdle();
    const uint32_t s0 = read32(GP1);
    write32(GP0, 0x03000000u);
    waitIdle();
    const uint32_t s1 = read32(GP1);
    ExitCriticalSection();
    printf("GPUSTAT %08x before, %08x after GP0(03h) = 0\n", (unsigned)s0, (unsigned)s1);
    printf("Done.\n");
    for (;;) { }
}
