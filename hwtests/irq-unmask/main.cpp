/*
 * cpu/irq-unmask - after the store that unmasks a pending interrupt, how many instructions run before the CPU takes it?
 *
 * WHY (2026-11-06). MLB 99 (USA) jumps through a NULL pointer (StartPAD before InitPAD) into the kernel's stub at address 0,
 * whose first word the BIOS shell's libcd has overwritten, so the jump goes to k0 + C80h, and k0 is the EPC the last interrupt
 * returned to. The interrupt is a VBlank latched by the game's GP1(00h) (gp1-reset / gp1-reset-480i: at once in the picture)
 * while I_MASK is 0, taken when SetIntrMask stores 9: `lhu v0,0(v1); sh a0,0(v1); jr ra; nop`. This core and DuckStation take
 * it on the `jr` (EPC 80021184h) and the game crashes on 15 of 16 CD timing draws (experiments/mlb99_draws.js); taken a few
 * instructions later, in the caller, k0 differs and so does where the jump lands. Nothing measured says which.
 *
 * WHAT IT DOES. Its own exception vector at 80000080h (restored after) that keeps t5, EPC and Cause and masks the interrupt
 * again; each row waits for a VBlank with I_MASK 0 (I_STAT.0 latched), turns SR.IEc and IM2 on, and runs a variant that stores
 * I_MASK = 1 and then counts in t5:
 *   GAME   MLB 99's SetIntrMask and its caller, instruction for instruction (cached).
 *   SW     a word store, then 16 counts.     SH  the same as a halfword (the game's width).
 *   WBUF   three RAM stores ahead of it.     LW  a RAM load right behind it.
 *   and SW, GAME again from KSEG1 (uncached code).
 * Each row: EPC as the instruction it names (relative to the variant), BD, t5 at the interrupt, interrupts taken. 2026-11-06,
 * Claude (AI) for AngryEmpathPS1.
 *
 * READ IT LIKE THIS. This core: GAME takes it on the jr (setmask+20, BD 0, t5 0); SW/SH on the first count (after+0, t5 0).
 * A console that takes it k instructions later reads after+k / t5 k, and for GAME the caller's instructions (ret+k).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070
#define I_MASK 0x1F801074
#define T2_COUNT 0x1F801120
#define LIMIT 2000000

extern "C" {
void irq_handler();
void irq_ien();
void irq_ioff();
void bios_flush();
uint32_t v_setmask(uint32_t);
uint32_t v_game();
uint32_t v_sw();
uint32_t v_sh();
uint32_t v_wbuf();
uint32_t v_lw();
extern uint32_t v_game_ret[], v_sw_after[], v_sh_after[], v_wbuf_after[], v_lw_after[];
extern volatile uint32_t irq_rec[4];
}

typedef uint32_t (*vfn)();

static int waitPending() {
    write32(I_MASK, 0);
    write32(I_STAT, ~1u);
    for (uint32_t n = 0; n < LIMIT; n++) if (read32(I_STAT) & 1) return 1;
    return 0;
}

/* where an EPC falls: an offset in instructions from the nearest named place */
static void place(uint32_t epc, uint32_t seg, char *out) {
    struct { const char *n; uint32_t a; } P[] = {
        { "setmask", (uint32_t)v_setmask }, { "ret", (uint32_t)v_game_ret }, { "sw.after", (uint32_t)v_sw_after },
        { "sh.after", (uint32_t)v_sh_after }, { "wbuf.after", (uint32_t)v_wbuf_after }, { "lw.after", (uint32_t)v_lw_after },
        { "game", (uint32_t)v_game }, { "sw", (uint32_t)v_sw }, { "sh", (uint32_t)v_sh }, { "wbuf", (uint32_t)v_wbuf }, { "lw", (uint32_t)v_lw },
    };
    uint32_t e = (epc & 0x1FFFFFFF) | 0x80000000;
    int best = -1; int32_t bd = 0;
    for (int i = 0; i < (int)(sizeof(P) / sizeof(P[0])); i++) {
        int32_t d = (int32_t)(e - P[i].a);
        if (d >= 0 && d < 0x100 && (best < 0 || d < bd)) { best = i; bd = d; }
    }
    if (best < 0) sprintf(out, "%08x", epc); else sprintf(out, "%s+%d", P[best].n, bd / 4);
    (void)seg;
}

static void row(const char *name, vfn f, int uncached) {
    if (!waitPending()) { printf("%-10s NO VBLANK\n", name); return; }
    irq_rec[0] = irq_rec[1] = irq_rec[2] = 0; irq_rec[3] = 0;
    vfn g = uncached ? (vfn)(((uint32_t)f & 0x1FFFFFFF) | 0xA0000000) : f;
    irq_ien();
    uint32_t v = g();
    irq_ioff();
    write32(I_MASK, 0);
    char where[32];
    place(irq_rec[1], 0, where);
    printf("%-10s EPC %-14s BD %d  t5 at it %2d  total %2d  taken %d  cause %08x\n", name, where, (int)(irq_rec[2] >> 31),
           (int)irq_rec[0], (int)v, (int)irq_rec[3], irq_rec[2]);
}

int main() {
    printf("\ncpu/irq-unmask\n");
    EnterCriticalSection();
    uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    volatile uint32_t *vec = (volatile uint32_t *)0x80000080;
    uint32_t saved[4] = { vec[0], vec[1], vec[2], vec[3] };
    uint32_t h = (uint32_t)irq_handler;
    vec[0] = 0x3C1A0000 | ((h + 0x8000) >> 16);      /* lui k0, %hi */
    vec[1] = 0x275A0000 | (h & 0xFFFF);              /* addiu k0, k0, %lo */
    vec[2] = 0x03400008;                             /* jr k0 */
    vec[3] = 0;
    bios_flush();

    for (int k = 0; k < 4; k++) {
        row("GAME", v_game, 0);
        row("SW", v_sw, 0);
        row("SH", v_sh, 0);
        row("WBUF", v_wbuf, 0);
        row("LW", v_lw, 0);
        row("SW unc", v_sw, 1);
        row("GAME unc", v_game, 1);
    }

    vec[0] = saved[0]; vec[1] = saved[1]; vec[2] = saved[2]; vec[3] = saved[3];
    bios_flush();
    write32(I_STAT, ~1u);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
