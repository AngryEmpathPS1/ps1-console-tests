/*
 * hwtests/exp-bracket - WHAT hwtests/exp-window's BRACKET ITSELF COSTS (2026-11-17, the busValue session; owner AngryEmpath).
 *
 * exp-window times one access between two reads of Timer 2, the access RIGHT BEHIND the first read. On the SCPH-7502 (no
 * cart) and the SCPH-1001 (the cart in), every Expansion 1 / 2 load and every bus error read 3 cycles over this core's, the
 * Expansion 3 loads 1 under (words 3 under), the Expansion 2 byte store 8 under - the same on both consoles, the same on two
 * runs. A uniform +3 on rows that share nothing but the bracket says the bracket: an access right behind an on-die load.
 * This test asks it directly: the same bracket with G = 0, 1, 2, 3, 4 no-ops between the Timer 2 read and the access, for
 *   nop (the empty bracket), lbu RAM, lbu scratchpad, lbu / lhu / lw Expansion 2 inside its window (1F802000h),
 *   lbu Expansion 2 past it (1F802080h: the bus error, a handler in place), lbu / lw Expansion 3 (1FA00000h),
 *   lbu / lw Expansion 1 (1F000000h, LOADS ONLY), sb Expansion 2 (1F802000h, never at POST), lbu the BIOS ROM.
 * A row that grows by exactly one cycle a no-op has no pair effect; one that drops or holds at G = 0..2 has one.
 * NOTHING IS WRITTEN in 1F000000h-1F07FFFFh; no register is changed but Timer 2's mode (put back) and the vector (put back).
 * There are no assertions - compare against your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u

extern "C" {
volatile uint32_t xb_cause, xb_epc, xb_value;
void xb_vector(void);
}
typedef uint32_t (*Op)(uint32_t);

#define XB(NAME, G, OP) \
    ".balign 16\n" \
    ".global " #NAME "\n" \
    #NAME ":\n" \
    "  lui   $t3, 0x1F80\n" \
    "  ori   $v1, $zero, 0x1EF7\n" \
    "  lhu   $t0, 0x1120($t3)\n" \
    "  .rept " #G "\n  nop\n  .endr\n" \
    "  " OP "\n" \
    "  .rept 8\n  nop\n  .endr\n" \
    "  lhu   $t1, 0x1120($t3)\n" \
    "  lui   $t4, %hi(xb_value)\n" \
    "  sw    $v1, %lo(xb_value)($t4)\n" \
    "  subu  $v0, $t1, $t0\n" \
    "  jr    $ra\n" \
    "  andi  $v0, $v0, 0xFFFF\n"
#define XB5(N, OP) XB(N##0, 0, OP) XB(N##1, 1, OP) XB(N##2, 2, OP) XB(N##3, 3, OP) XB(N##4, 4, OP)
#define DECL5(N) uint32_t N##0(uint32_t); uint32_t N##1(uint32_t); uint32_t N##2(uint32_t); uint32_t N##3(uint32_t); uint32_t N##4(uint32_t);
extern "C" { DECL5(xb_nop) DECL5(xb_lbu) DECL5(xb_lhu) DECL5(xb_lw) DECL5(xb_sb) }

__asm__(
    ".text\n"
    ".set push\n.set noreorder\n.set noat\n"
    ".balign 16\n"
    ".global xb_vector\n"
    "xb_vector:\n"
    "  lui   $k0, %hi(xb_handler)\n"
    "  addiu $k0, $k0, %lo(xb_handler)\n"
    "  jr    $k0\n"
    "  nop\n"
    "xb_handler:\n"
    "  mfc0  $k1, $13\n"
    "  lui   $k0, %hi(xb_cause)\n"
    "  sw    $k1, %lo(xb_cause)($k0)\n"
    "  mfc0  $k1, $14\n"
    "  lui   $k0, %hi(xb_epc)\n"
    "  sw    $k1, %lo(xb_epc)($k0)\n"
    "  addiu $k0, $k1, 4\n"
    "  jr    $k0\n"
    "  rfe\n"
    XB5(xb_nop, "nop")
    XB5(xb_lbu, "lbu $v1, 0($a0)")
    XB5(xb_lhu, "lhu $v1, 0($a0)")
    XB5(xb_lw, "lw $v1, 0($a0)")
    XB5(xb_sb, "sb $v1, 0($a0)")
    ".set pop\n");

static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

static uint32_t ramWord = 0x11223344u;
#define ROWS 13
static const struct { const char *name; Op op[5]; uint32_t addr; } rows[ROWS] = {
    { "nop (the empty bracket)      ", { xb_nop0, xb_nop1, xb_nop2, xb_nop3, xb_nop4 }, 0 },
    { "lbu RAM                      ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 1 },
    { "lbu scratchpad 1F800000h     ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0x1F800000u },
    { "lbu Expansion 2 1F802000h    ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0x1F802000u },
    { "lhu Expansion 2 1F802000h    ", { xb_lhu0, xb_lhu1, xb_lhu2, xb_lhu3, xb_lhu4 }, 0x1F802000u },
    { "lw  Expansion 2 1F802000h    ", { xb_lw0, xb_lw1, xb_lw2, xb_lw3, xb_lw4 }, 0x1F802000u },
    { "lbu Expansion 2 1F802080h    ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0x1F802080u },
    { "lbu Expansion 3 1FA00000h    ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0x1FA00000u },
    { "lw  Expansion 3 1FA00000h    ", { xb_lw0, xb_lw1, xb_lw2, xb_lw3, xb_lw4 }, 0x1FA00000u },
    { "lbu Expansion 1 1F000000h    ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0x1F000000u },
    { "lw  Expansion 1 1F000000h    ", { xb_lw0, xb_lw1, xb_lw2, xb_lw3, xb_lw4 }, 0x1F000000u },
    { "sb  Expansion 2 1F802000h    ", { xb_sb0, xb_sb1, xb_sb2, xb_sb3, xb_sb4 }, 0x1F802000u },
    { "lbu BIOS ROM BFC00000h       ", { xb_lbu0, xb_lbu1, xb_lbu2, xb_lbu3, xb_lbu4 }, 0xBFC00000u },
};
static uint16_t cyc[ROWS][5];
static uint8_t err[ROWS][5];

int main() {
    printf("\nbus/exp-bracket\n");
    printf("exp-window's bracket: Timer 2, G no-ops, one access, eight no-ops, Timer 2 - what the access costs right\n");
    printf("behind the timer's read (G = 0, as exp-window has it) and 1-4 instructions on. No assertions.\n\n");
    volatile uint32_t *const vec = (volatile uint32_t *)0x80000080u;
    uint32_t saved[4];
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int i = 0; i < 4; i++) { saved[i] = vec[i]; vec[i] = ((const uint32_t *)xb_vector)[i]; }
    flushCache();
    for (int pass = 0; pass < 3; pass++)             /* the last pass is kept: every routine and the handler warm */
        for (int r = 0; r < ROWS; r++) for (int g = 0; g < 5; g++) {
            /* the store row writes only inside Expansion 2; the guard is the hard rule, said in code */
            const uint32_t a = rows[r].addr == 1 ? (uint32_t)&ramWord : rows[r].addr;
            if (rows[r].op[0] == xb_sb0 && (a < 0x1F802000u || a > 0x1F80203Fu)) continue;
            xb_cause = 0;
            const uint32_t c = rows[r].op[g](a);
            cyc[r][g] = (uint16_t)c; err[r][g] = (uint8_t)((xb_cause >> 2) & 0x1F) | (xb_cause ? 0x80 : 0);
        }
    for (int i = 0; i < 4; i++) vec[i] = saved[i];
    flushCache();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("  access                          G=0   G=1   G=2   G=3   G=4     (cycles; * = it raised an exception)\n");
    for (int r = 0; r < ROWS; r++) {
        printf("  %s", rows[r].name);
        for (int g = 0; g < 5; g++) printf(" %4u%c", (unsigned)cyc[r][g], err[r][g] ? '*' : ' ');
        if (err[r][0]) printf("    excode %02u", (unsigned)(err[r][0] & 0x1F));
        printf("\n");
    }
    printf("\nDone.\n");
    return 0;
}
