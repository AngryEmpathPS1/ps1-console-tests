/*
 * fuzz-crumb - what a hwtests/fuzz-all run left in RAM, read after a RESET (2026-11-13)
 *
 * WHY. The SCPH-7502 stopped answering for good during experiments/fuzz-all/2026-11-13/shrink-r1.exe (a program the second
 * SCPH-5501 finished in 16 s) and needed a RESET. A RESET keeps RAM (tools/freezedump.js), and the fuzz host writes a
 * breadcrumb before and after each case's runs: 8012FC00h the case id, 8012FC04h 1 while it runs, 2 once it has finished.
 * Run this FIRST on a console a fuzz batch hung, before anything else loads over those addresses.
 *
 * WHAT IT DOES. Reads and prints, writing nothing there: the breadcrumb (8012FC00h, 4 words), the case table's header
 * (80110000h: magic 46555A31h, the case count, flags, seed, words), the case page's first 8 words (80100000h, the last case
 * placed there) and the save area's first 4 words (80104000h). This program loads below 80100000h and leaves them alone.
 *
 * READ IT LIKE THIS. W address value. A table magic other than 46555A31 means another program has loaded over the host since.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static void dump(uint32_t a, int n) { for (int i = 0; i < n; i++) printf("W %x %x\n", a + 4 * i, *(volatile uint32_t *)(a + 4 * i)); }

int main() {
    printf("\nfuzz-crumb\n");
    dump(0x8012FC00u, 4);
    dump(0x80110000u, 5);
    dump(0x80100000u, 8);
    dump(0x80104000u, 4);
    printf("\nfuzz-crumb: Done.\n");
    for (;;) VSync(0);
}
