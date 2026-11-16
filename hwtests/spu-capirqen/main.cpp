/*
 * spu/spu-capirqen - is the capture ring's write of the IRQ address taken when SPUCNT's IRQ enable comes just before it? (2026-11-16)
 * The rule (EN) of ../spu-capirq-common.h, which has the whole design and the line format: 120 trials, the enable written 0-3,071
 * cycles before the ring's write of 800h. THE RULE: run here first: `node hwtests/run.js spu-capirqen --frames=12000 --quiet=4000`.
 */
#include "spu-capirq-common.h"
int main() {
    printf("\nspu/spu-capirqen\nThe IRQ enable written D cycles before the capture ring writes the IRQ address: C trial D A irq. No assertions.\n\n");
    run(120, 0, 3072);
    printf("\nspu/spu-capirqen: Done.\n");
    for (;;) VSync(0);
}
