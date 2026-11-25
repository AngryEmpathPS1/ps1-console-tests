/*
 * cart-cleandump (2026-11-25, the cart session): THE WHOLE 256 KB OF A CART, FOUR TIMES, AT THE EXPANSION 1 DELAYS THAT READ CLEAN. The SCPH-1000's GameShark 2.3, on the 1001-u2 after its contacts were cleaned
 * (cart-readnoise: 0 bad bytes in 4 x 32 KB at 0013240Fh, 0013241Fh, 0013200Fh, 0013000Fh; 4-6% at the kernel's 0013243Fh). Pass p reads 1F000000h-1F01FFFFh then 1F040000h-1F05FFFFh into RAM at 80020000h + p*40000h;
* (NOT above 80130000h: that is the resident runner's - my first build ran to 801C0000h and hung the 1001-u2 until its reset, 2026-11-25) the PC fetches the four copies with the runner's M and compares them (tools: experiments/cartclean.js). Reads the cart only; writes the EXP1 delay register (restored) and the RAM buffers.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>
#define EXP1_DELAY (*(volatile uint32_t *)0x1F801008u)
#define I_MASK     (*(volatile uint32_t *)0x1F801074u)
int main() {
    printf("\ncart-cleandump\n");
    const uint32_t e1 = EXP1_DELAY, im = I_MASK;
    const uint32_t delays[4] = { 0x0013240Fu, 0x0013241Fu, 0x0013200Fu, 0x0013000Fu };
    for (int p = 0; p < 4; p++) {
        volatile uint8_t *dst = (volatile uint8_t *)(0xA0020000u + p * 0x40000u);   // uncached alias of 80020000h + p*40000h
        I_MASK = 0; EXP1_DELAY = delays[p];
        const volatile uint8_t *a = (const volatile uint8_t *)0xBF000000u, *b = (const volatile uint8_t *)0xBF040000u;
        for (int i = 0; i < 0x20000; i++) dst[i] = a[i];
        for (int i = 0; i < 0x20000; i++) dst[0x20000 + i] = b[i];
        EXP1_DELAY = e1; I_MASK = im;
        printf("pass %d at delay %08X read\n", p, (unsigned)delays[p]);
    }
    unsigned d1 = 0, d2 = 0, d3 = 0;
    const volatile uint8_t *r0 = (const volatile uint8_t *)0xA0020000u;
    for (int i = 0; i < 0x40000; i++) { d1 += r0[i] != r0[0x40000 + i]; d2 += r0[i] != r0[0x80000 + i]; d3 += r0[i] != r0[0xC0000 + i]; }
    printf("bytes of pass 0 that differ from pass 1: %u, pass 2: %u, pass 3: %u (of 262144)\nDone.\n", d1, d2, d3);
    for (;;) {}
}
