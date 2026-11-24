/*
 * cpu/at-romloop2 (2026-11-23, the cold-io session) - at-romloop with its second blob (blob.h here): loads whose value is used,
 * two loads, two stores and the copy loop's shapes, from code fetched out of a ROM. at-romloop's header follows, word for word:
 *
 * cpu/at-romloop - a store and a load from code fetched out of a ROM,
 * at chosen spacings: the loops in blob.h, run from the cart's flash in EXPANSION 1, from uncached RAM and from cached RAM.
 *
 * WHY. ROM_FETCH_EXTRA's KNOWN-OPEN and land_romhold's (STATUS "land_romhold proposed", "ROM code's store-only term"): the BIOS census
 * shows a store alone in a ROM loop (bzero, memset) ~1.3 dearer a store on the consoles than here and a store behind a load (memcpy,
 * strcpy) slower here, and no BIOS function has the spacings that would split them. Code in the cart's flash is fetched the same way
 * (an uncached fetch of a slow region on the 8-bit bus; EXP1's delay register here set to the kernel's 0013243Fh, the BIOS ROM's own value, and
 * COM_DELAY to the kernel's 00031125h, both put back after each row).
 *
 * WHAT IT DOES. Finds blob.h's words in the EXPANSION 1 window (1F000000h .. 1F07FFFFh, both of a Datel cart's chips: 1F000000h and 1F040000h; by its 'AERLOOP2' magic, every word checked)
 * and prints NOCART if they are not there. Interrupts masked, Timer 2 at the system clock; each function called from cached code,
 * Timer 2 read before and after, the fastest of 8 (a first call dropped), at its cart address (KSEG1), its RAM copy's KSEG1 address
 * (uncached RAM) and its KSEG0 address (cached). A row: the function, then cart / uncached RAM / cached cycles for its 64 turns.
 * READ (row - N) / 64 as what the slots cost a turn; S0 - N the store alone, S1..S3 - L0 a store 1..3 behind a load.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-romloop2` (NOCART without a cart).
 */

#include <common.h>
#include "blob.h"

#define EXP1_DELAY (*(volatile uint32_t *)0x1F801008u)
#define I_MASK     (*(volatile uint32_t *)0x1F801074u)
#define COM_DELAY  (*(volatile uint32_t *)0x1F801020u)

static uint32_t ramcopy[RL_WORDS] __attribute__((aligned(16)));
static volatile uint8_t dst[128] __attribute__((aligned(16))), src[128] __attribute__((aligned(16)));

extern "C" uint32_t rl_call(uint32_t fn, uint32_t a0, uint32_t a1);
__asm__(".set push\n.set noreorder\n.global rl_call\nrl_call:\n"
        "  addiu $sp, $sp, -8\n  sw $ra, 4($sp)\n  move $t2, $a0\n  move $a0, $a1\n  move $a1, $a2\n  lui $t8, 0x1F80\n"
        "  lhu $t6, 0x1120($t8)\n  jalr $t2\n  nop\n  lhu $t7, 0x1120($t8)\n  nop\n"
        "  subu $v0, $t7, $t6\n  andi $v0, $v0, 0xFFFF\n  lw $ra, 4($sp)\n  nop\n  jr $ra\n  addiu $sp, $sp, 8\n"
        ".set pop\n");

static uint32_t best(uint32_t fn) {
    uint32_t m = ~0u;
    for (int i = -1; i < 8; i++) { const uint32_t t = rl_call(fn, (uint32_t)dst, (uint32_t)src); if (i >= 0 && t < m) m = t; }
    return m;
}

int main() {
    printf("\ncpu/at-romloop\n");
    printf("a store and a load at chosen spacings, 64 turns, from the cart's flash (EXPANSION 1), uncached RAM and cached RAM; Timer 2 cycles, fastest of 8.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    for (int i = 0; i < RL_WORDS; i++) ramcopy[i] = RL_BLOB[i];
    uint32_t cart = 0;
    for (uint32_t a = 0xBF000000u; a < 0xBF080000u && !cart; a += 16) {
        const volatile uint32_t *p = (const volatile uint32_t *)a;
        if (p[0] != RL_BLOB[0] || p[1] != RL_BLOB[1]) continue;
        int ok = 1; for (int i = 0; i < RL_WORDS; i++) if (p[i] != RL_BLOB[i]) { ok = 0; break; }
        if (ok) cart = a;
    }
    if (cart) printf("cart blob at %08X\n", (unsigned)cart); else printf("NOCART (blob.h's words not found in 1F000000h-1F07FFFFh)\n");
    *(volatile uint16_t *)0x1F801124u = 0;   // Timer 2: system clock, free run
    const uint32_t e1 = EXP1_DELAY, im = I_MASK, com = COM_DELAY;
    printf("found EXP1 delay %08X, COM_DELAY %08X; each row runs at 0013243Fh and 00031125h (the kernel's)\n", (unsigned)e1, (unsigned)com);
    printf("fn     cart  uncRAM  cached\n");
    for (int f = 0; f < RL_NFN; f++) {
        const uint32_t off = RL_BLOB[3 + f], ram = (uint32_t)ramcopy + off;
        I_MASK = 0; EXP1_DELAY = 0x0013243Fu; COM_DELAY = 0x00031125u;
        const uint32_t c = cart ? best(cart + off) : 0, u = best((ram & 0x1FFFFFFFu) | 0xA0000000u), k = best((ram & 0x1FFFFFFFu) | 0x80000000u);
        EXP1_DELAY = e1; COM_DELAY = com; I_MASK = im;
        printf("%-4s %6u %7u %7u\n", RL_NAMES[f], (unsigned)c, (unsigned)u, (unsigned)k);
    }
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
