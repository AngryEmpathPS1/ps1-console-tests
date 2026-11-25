/*
 * cart-idprobe (2026-11-25, the cart session; AngryEmpath: "write the unknown chip, it goes in the trash if it does not work"). WHICH FLASH IS IT? The id op of the flash tools (SST29EE020 / AM29F010: unlock at 5555h / 2AAAh, command 90h) read 2F 6F for both ID bytes on the SCPH-1000's GameShark 2.3
 * whatever the read; this tries the other standard ID-entry sequences and prints the first three bytes after each, then leaves ID mode (F0h, FFh, and the 3-cycle AAh 55h F0h) and prints the first bytes of the ROM so the cart is seen to be back to normal. It writes ONLY command bytes (never a data
 * address of the ROM image beyond the unlock cycles) and nothing is erased or programmed by any sequence here (90h and F0h/FFh are ID / reset commands on every chip family that has them).
 * Sequences (cycle addresses are byte offsets from 1F000000h): A 5555/2AAA/5555 (SST, AMD 29F010, Atmel AT29); B 0555/02AA/0555 (AMD 29F040 / 29LV, 11-bit address decoders); C AAAA/5554/AAAA (x16 style address shift); D 90h at 0 alone (Intel 28F);
 * E 5555/2AAA/5555 with the data bytes AAh/55h/90h but each cycle written twice (a slow bus); F 0AAA/0555/0AAA (x8 AMD, 12-bit).
 */
#include <common.h>
#include <stdio.h>

#define EXP1_DELAY (*(volatile uint32_t *)0x1F801008u)
#define I_MASK     (*(volatile uint32_t *)0x1F801074u)
static inline void wr(uint32_t off, uint8_t v) { *(volatile uint8_t *)(0xBF000000u + off) = v; }
static inline uint8_t rd(uint32_t off) { return *(volatile uint8_t *)(0xBF000000u + off); }
static void wait(int n) { for (volatile int i = 0; i < n * 400; i++) {} }
static void reset_modes() {
    wr(0, 0xF0); wait(2); wr(0, 0xFF); wait(2);
    wr(0x5555, 0xAA); wr(0x2AAA, 0x55); wr(0x5555, 0xF0); wait(20);     // SST / AMD software exit
    wr(0x555, 0xAA); wr(0x2AA, 0x55); wr(0x555, 0xF0); wait(20);
}
static void show(const char *name) {
    printf("%-34s %02X %02X %02X %02X | ", name, rd(0), rd(1), rd(2), rd(3));
    printf("%02X %02X %02X %02X\n", rd(0x100), rd(0x101), rd(0x1FFFE), rd(0x1FFFF));
}
int main() {
    printf("\ncart-idprobe\n");
    const uint32_t e1 = EXP1_DELAY, im = I_MASK;
    I_MASK = 0; EXP1_DELAY = 0x0013243Fu;
    printf("sequence                           bytes 0-3  | bytes 100h,101h, 1FFFEh,1FFFFh\n");
    reset_modes(); show("ROM as it reads (no command)");
    reset_modes(); show("ROM again");
    struct S { const char *n; uint32_t a1, a2, a3; uint8_t d3; int dbl; } seq[] = {
        { "A 5555/2AAA/5555  AA 55 90", 0x5555, 0x2AAA, 0x5555, 0x90, 0 },
        { "B 0555/02AA/0555  AA 55 90", 0x555, 0x2AA, 0x555, 0x90, 0 },
        { "C AAAA/5554/AAAA  AA 55 90", 0xAAAA, 0x5554, 0xAAAA, 0x90, 0 },
        { "F 0AAA/0555/0AAA  AA 55 90", 0xAAA, 0x555, 0xAAA, 0x90, 0 },
        { "E 5555/2AAA/5555 doubled", 0x5555, 0x2AAA, 0x5555, 0x90, 1 },
        { "A' 5555/2AAA/5555  AA 55 80 (erase setup: NOT used)", 0, 0, 0, 0, -1 },
    };
    for (int i = 0; i < 5; i++) {
        reset_modes();
        for (int rep = 0; rep < (seq[i].dbl ? 2 : 1); rep++) { wr(seq[i].a1, 0xAA); wr(seq[i].a2, 0x55); wr(seq[i].a3, seq[i].d3); }
        wait(20);
        show(seq[i].n);
    }
    reset_modes(); wr(0, 0x90); wait(20); show("D 90h at 0 (Intel)");
    reset_modes(); wr(0, 0x98); wait(20); show("D2 98h at 0 (CFI query)");
    reset_modes(); show("after resets: the ROM again");
    EXP1_DELAY = e1; I_MASK = im;
    printf("Done.\n");
    for (;;) {}
}
