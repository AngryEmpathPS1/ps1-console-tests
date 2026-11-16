/*
 * spu/spu-capirqhw - does the capture ring's write of a LATER halfword of the IRQ address's 8-byte block fire the IRQ? (2026-11-16)
 * The rule (HW) of ../spu-capirq-common.h, which has the whole design and the line format: 120 trials, the enable written 50-3,000
 * cycles after the ring's write of 800h. THE RULE: run here first: `node hwtests/run.js spu-capirqhw --frames=12000 --quiet=4000`.
 */
#include "spu-capirq-common.h"
int main() {
    printf("\nspu/spu-capirqhw\nThe IRQ enable written after the capture ring wrote the IRQ address, inside its 8-byte block: C trial D A irq. No assertions.\n\n");
    run(120, -3000, -50);
    printf("\nspu/spu-capirqhw: Done.\n");
    for (;;) VSync(0);
}
