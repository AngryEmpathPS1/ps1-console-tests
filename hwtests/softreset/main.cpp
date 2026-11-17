/*
 * softreset - a SOFT RESET from the runner, hands-free (AngryEmpath's ask for cart ROM v2, 2026-11-17; owner AngryEmpath).
 *
 * Sent and started as any test ('L', then 'R'), it never prints "Done.": it jumps to the reset vector, BFC00000h, with
 * interrupts off, so the BIOS boots again, the cart's Pre-Boot sends a fresh frame, and the runner comes back from the cart
 * (its PSXRUNNER banner). With no cart, the BIOS boots to the shell or a card, as after RESET. A test, not a runner command, on
 * purpose: the runner's build is a start variable of its own (docs/INSTRUMENT-AUDIT-2.md finding 1: 34 programs print one log per
 * runner build on every console), so this changes nothing a test starts from.
 *
 * NOT A RESET: the hardware RESET line resets the GPU, the SPU, the CD controller and the CPU's COP0; this jump resets none of
 * them, and RAM keeps everything. So it MARKS the boot: RAM 4Ch, where cart ROM v2 keeps AE5Ah:count (cartrom/rom.c
 * power_marker), becomes AE5Bh:count. The next Pre-Boot frame's POWER.WARM reads 2 (soft), where a RESET or power cycle reads
 * 1 or 0, and its snapshots are never pooled with theirs.
 */
#include <stdint.h>

int main() {
    __asm__ volatile("mtc0 $zero, $12\n nop\n nop");                  /* SR 0: interrupts off, no isolation (the cart reads SR) */
    __asm__ volatile("mtc0 $zero, $7\n nop");                          /* DCIC off: no break armed from the last boot */
    volatile uint32_t *mark = (volatile uint32_t *)0xA000004Cu;
    const uint32_t m = *mark;
    const uint32_t hi = m >> 16, n = (hi == 0xAE5Au || hi == 0xAE5Bu) ? (m & 0xFFFFu) : 0;
    *mark = (0xAE5Bu << 16) | n;                                        /* the cart adds 1 to the count */
    for (volatile int i = 0; i < 20000; i++) {}                        /* the runner's last byte out of SIO1 first */
    __asm__ volatile("lui $t0, 0xBFC0\n jr $t0\n nop" ::: "t0");
    for (;;) {}
}
