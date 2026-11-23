/*
 * soft-reset - A JUMP TO THE BIOS'S RESET VECTOR (2026-11-23, the cart session, main for AngryEmpath: "reset 3415" when no wire reaches /RESET). Not a test: the GameShark console's way to boot again, so the cart's own Pre-Boot
 * (which the BIOS reaches through the expansion ROM header at its start) runs once more with the Pico's boot command queued. The runner is idle when this starts (restoreKernel has run: DMA idle, interrupts as the kernel
 * leaves them); this masks every interrupt source, stops the DMA channels' enables and the timers' interrupt requests, disables the CPU's interrupts and jumps to BFC00000h. It prints a line first so the log says it was sent.
 * What it is NOT: a hardware reset (the GPU, SPU, CD and timers keep their state until the BIOS touches them), so it does not stand in for the console's own power-on state - use it only to reach the cart's boot path.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>

int main() {
    printf("\nsoft-reset: jumping to the BIOS reset vector\n");   /* no "Done." line: the runner takes the CPU back at it, and the jump below would never run */
    for (volatile int i = 0; i < 2000000; i++) {}          /* the line leaves the UART before the CPU goes */
    *(volatile uint32_t *)0x1F801074 = 0;                   /* I_MASK */
    *(volatile uint32_t *)0x1F801070 = 0;                   /* I_STAT (writing 0 acknowledges everything) */
    uint32_t sr;
    asm volatile("mfc0 %0, $12" : "=r"(sr));
    sr &= ~1u;                                              /* IEc off */
    asm volatile("mtc0 %0, $12" :: "r"(sr));
    void (*reset)() = (void (*)())0xBFC00000;
    reset();
    return 0;
}
