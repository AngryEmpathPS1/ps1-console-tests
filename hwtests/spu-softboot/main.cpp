/*
 * spu/spu-softboot - A SOFT RESET WITH THE CAPTURE RING HELD: how long is a boot, in SPU samples? (2026-11-25, the SPU session;
 * owner AngryEmpath's "console perfect" bar - each model's own boot-to-boot spread, not just its mean.)
 *
 * WHY. The SPU's one boot draw is where its capture ring sits against the envelope ruler (hwtests/spu-bootprobe reads it; the
 * key-on class is that number's parity). The ring stands still while the transfer control (1F801DACh) holds a Fill type and runs
 * in Normal (hwtests/spu-capstep, eight consoles); a power-on leaves the register 0000h and every BIOS writes it once, 0004h, in
 * the shell's sound start, ~2.2-2.4 s in (experiments/spu_biosxfer.js). So a boot's draw is the length of that stretch in samples -
 * and how much it varies from boot to boot is the draw's spread. A power cycle needs a hand. This does not: hwtests/softreset's jump
 * to the reset vector, with one store first - the transfer control set to PARAM (000Ch: a Fill type that stops the ring on every
 * board, the SCPH-1000 / 1001's included, whose types 0 and 1 step it 4 and 2). The BIOS boots, its shell writes 0004h, the cart's
 * boot runs spu-bootprobe (kept at 80190500h; tools/cartread.js), and the phase has moved by the samples in between.
 * experiments/spu_softboot.js sends it, reads the probe and repeats.
 *
 * PARAM is the word after the marker 5B0D5A17h in this file's data: FFFFFFFFh = no store (a plain soft reset, the control: the
 * phase must not move), else its low 16 bits are written. The sender patches it; as built it is FFFFFFFFh.
 * Never prints "Done." (see hwtests/softreset: the same marks at RAM 4Ch, WARM 2). A CART console only: elsewhere the BIOS boots
 * to the shell and nothing brings a runner back.
 */
#include <stdint.h>

__attribute__((used)) volatile uint32_t softboot_param[2] = { 0x5B0D5A17u, 0xFFFFFFFFu };

int main() {
    __asm__ volatile("mtc0 $zero, $12\n nop\n nop");                  /* SR 0: interrupts off (the cart reads SR) */
    __asm__ volatile("mtc0 $zero, $7\n nop");                          /* DCIC off */
    volatile uint32_t *mark = (volatile uint32_t *)0xA000004Cu;
    const uint32_t m = *mark;
    const uint32_t hi = m >> 16, n = (hi == 0xAE5Au || hi == 0xAE5Bu) ? (m & 0xFFFFu) : 0;
    *mark = (0xAE5Bu << 16) | n;                                        /* the cart adds 1 to the count */
    for (volatile int i = 0; i < 20000; i++) {}                        /* the runner's last byte out of SIO1 first */
    const uint32_t p = softboot_param[1];
    if (p != 0xFFFFFFFFu) *(volatile uint16_t *)0x1F801DACu = (uint16_t)p;   /* the ring stops here ... */
    __asm__ volatile("lui $t0, 0xBFC0\n jr $t0\n nop" ::: "t0");       /* ... and runs again at the shell's 0004h */
    for (;;) {}
}
