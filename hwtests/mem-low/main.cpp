/*
 * cpu/mem-low - the words at physical 0 (the UTLB vector's copy), 80h (the exception vector) and the A0/B0/C0 vectors, as the
 * console holds them when the runner starts this. WHY (2026-11-06): MLB 99 (USA) jumps through NULL into address 0; the
 * BIOS shell's libcd stores 0..3 there (`sw t1,0(zero)`, v2.2 80058788h), which turns the kernel's `lui k0,0` into `sra
 * zero,zero,0` (STATUS "MLB 99's freeze traced to the end"). If the shell ran (it does at every power-on, a FreePSXBoot
 * boot included) and nothing restored it, word 0 reads 00000003. Reads only. 2026-11-06, Claude (AI) for AngryEmpathPS1.
 */
#include <common.h>
#include <psxapi.h>

static void dump(const char *name, uint32_t a, int n) {
    printf("%-8s %08x:", name, a);
    for (int i = 0; i < n; i++) printf(" %08x", ((volatile uint32_t *)a)[i]);
    printf("\n");
}

int main() {
    printf("\ncpu/mem-low\n");
    for (int k = 0; k < 2; k++) {
        dump("utlb", 0x80000000, 4);
        dump("exc", 0x80000080, 4);
        dump("a0", 0x800000a0, 4);
        dump("b0", 0x800000b0, 4);
        dump("c0", 0x800000c0, 4);
        dump("kuseg0", 0x00000000, 4);
        dump("kseg1", 0xa0000000, 4);
    }
    printf("\nDone.\n");
    for (;;) {}
}
