/*
 * cpu/icache-place8 - where does a cold read stop waiting behind a store: the 64-byte block edge, in cold code.
 *
 * WHY (2026-11-13). hwtests/icache-place6 (21156c14) on the second SCPH-5501 and the PSone: sw t0 at a line's word 2, lw t0 at word
 * 3 (the next line cold), every line: the load at the stored word or the next (+0, +4) ~+560 cycles over 128 lines against this
 * core, at +64 or +1028 +60 - a read behind a store in its own block waits for it there, where the stream lets it go behind the
 * next fill (STATUS "icache-place6's cold rows, after the store-free bound"). This finds the edge: the load at +0 .. +128 from the
 * store, the store at a 64-byte-aligned word (base 0) and 48 bytes into a block (base 48), so a 64-byte rule moves its edge
 * between the two and a 16-byte or same-word rule does not.
 * READ IT LIKE THIS. cold rows: +4 a line over this core where the read waits; warm rows the place6 warm +1.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE (*(volatile uint32_t *)0x1F801124)
#define I_MASK  (*(volatile uint32_t *)0x1F801074)
#define CODE ((volatile uint32_t *)0x80030000)
#define DATA 0x800C0000u
#define LINES 128

extern "C" {
uint32_t ip_call(uint32_t fn, uint32_t a0, uint32_t a1);
void ip_flush(void);
}
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".balign 16\n.global ip_call\n"
    "ip_call:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n"
    "  move  $t2, $a0\n  move $a0, $a1\n  move $a1, $a2\n"
    "  lui   $t3, 0x1F80\n"
    "  lhu   $s0, 0x1120($t3)\n"
    "  jalr  $t2\n"
    "  nop\n"
    "  lui   $t3, 0x1F80\n"
    "  lhu   $v0, 0x1120($t3)\n"
    "  nop\n"
    "  subu  $v0, $v0, $s0\n  andi $v0, $v0, 0xFFFF\n"
    "  lw $ra, 20($sp)\n  lw $s0, 16($sp)\n  jr $ra\n  addiu $sp, $sp, 24\n"
    ".global ip_flush\n"
    "ip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n"
    ".set pop\n");

#define NOP 0u
#define JR_RA 0x03E00008u
#define LW_T0 0x8C880000u      /* lw t0, 0(a0) */
#define ADDU_A0 0x00852021u    /* addu a0, a0, a1 */
#define BASE 0x80030000u

#define SW_T0 0xAC880000u      /* sw t0, 0(a0) */
#define ADDIU_A0 0x24840404u   /* addiu a0, a0, 1028 */
static uint32_t gen(uint32_t ws, uint32_t wl, uint32_t off) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES; i++) { CODE[i * 4 + ws] = SW_T0; CODE[i * 4 + wl] = LW_T0 | (off & 0xFFFF); }
    CODE[LINES * 4] = JR_RA;
    return BASE;
}

static void run(const char *name, uint32_t fn, uint32_t a1) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, DATA + a1, 0);
        uint32_t wv = ip_call(fn, DATA + a1, 0);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (wv < wmin) wmin = wv; if (wv > wmax) wmax = wv;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place8\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    { uint32_t f = gen(0, 0, 0); for (uint32_t i = 0; i < LINES * 4; i++) CODE[i] = NOP; run("nops", f, 0); }
    static const uint32_t O[11] = { 0, 4, 8, 12, 16, 32, 48, 60, 64, 68, 128 };
    for (uint32_t b = 0; b < 2; b++) for (int o = 0; o < 11; o++) {
        char nm[20]; int k = 0; nm[k++] = 'b'; if (b) { nm[k++] = '4'; nm[k++] = '8'; } else nm[k++] = '0'; nm[k++] = ' '; nm[k++] = '+';
        uint32_t v = O[o]; char t[6]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) nm[k++] = t[--n]; nm[k] = 0;
        run(nm, gen(2, 3, O[o]), b ? 48 : 0);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
