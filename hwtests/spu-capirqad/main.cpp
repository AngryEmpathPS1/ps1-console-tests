/*
 * spu/spu-capirqad - is the capture ring's write taken when the IRQ ADDRESS is moved onto it just before? (2026-11-16)
 * spu-capirqen's companion (spu-capirq-common.h has the design and line format), built with ADDRESS_MOVE: the IRQ is on at 900h
 * (which the ring reaches 128 samples on) and the address register is written to 800h D = 0-3,071 cycles before the ring writes
 * 800h. Redux's spu-dbuf rep 3 writes the address 369 cycles and the enable 341 before that write; spu-capirqen found the enable
 * taken from ~35 cycles before, so if rep 3 misses on the consoles, the address is the later one.
 * THE RULE: run here first: `node hwtests/run.js spu-capirqad --frames=12000 --quiet=4000`.
 */
#define ADDRESS_MOVE 1
#include "spu-capirq-common.h"
int main() {
    printf("\nspu/spu-capirqad\nThe IRQ address moved onto the capture ring's write D cycles before it, the IRQ on: C trial D A irq. No assertions.\n\n");
    run(120, 0, 3072);
    printf("\nspu/spu-capirqad: Done.\n");
    for (;;) VSync(0);
}
