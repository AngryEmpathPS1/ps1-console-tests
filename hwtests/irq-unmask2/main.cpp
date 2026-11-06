/*
 * cpu/irq-unmask2 - irq-unmask found the console running MORE instructions than this core after the store that unmasks a pending
 * VBlank (SW 4 instructions, SH 2, a RAM load behind 7; this core 0-1). Is that a time or a count? The same store, then fillers of
 * other costs: ADD (1 cycle), SB (the byte store), IOREAD (an I/O read just before it, as MLB 99's lhu), LW (a RAM load + a count),
 * LWUSE (a load used after a nop), BRANCH (a taken branch with the count in its slot), MULT (mult + mflo + a count), IOAFTER (an
 * I_STAT read + a count). Each row: EPC as the instruction it names, BD, t5 at it (counts done). 2026-11-06, Claude (AI) for
 * AngryEmpathPS1.
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
uint32_t v_add();
uint32_t v_sb();
uint32_t v_ioread();
uint32_t v_lw();
uint32_t v_lwuse();
uint32_t v_branch();
uint32_t v_mult();
uint32_t v_ioafter();
extern uint32_t v_add_after[], v_sb_after[], v_ioread_after[], v_lw_after[], v_lwuse_after[], v_branch_after[], v_mult_after[], v_ioafter_after[];
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
        { "add.after", (uint32_t)v_add_after },
        { "sb.after", (uint32_t)v_sb_after },
        { "ioread.after", (uint32_t)v_ioread_after },
        { "lw.after", (uint32_t)v_lw_after },
        { "lwuse.after", (uint32_t)v_lwuse_after },
        { "branch.after", (uint32_t)v_branch_after },
        { "mult.after", (uint32_t)v_mult_after },
        { "ioafter.after", (uint32_t)v_ioafter_after },
        { "add", (uint32_t)v_add }, { "sb", (uint32_t)v_sb }, { "ioread", (uint32_t)v_ioread }, { "lw", (uint32_t)v_lw }, { "lwuse", (uint32_t)v_lwuse }, { "branch", (uint32_t)v_branch }, { "mult", (uint32_t)v_mult }, { "ioafter", (uint32_t)v_ioafter },
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

    for (int k = 0; k < 3; k++) {
        row("ADD", v_add, 0);
        row("SB", v_sb, 0);
        row("IOREAD", v_ioread, 0);
        row("LW", v_lw, 0);
        row("LWUSE", v_lwuse, 0);
        row("BRANCH", v_branch, 0);
        row("MULT", v_mult, 0);
        row("IOAFTER", v_ioafter, 0);
        row("ADD unc", v_add, 1);
        row("LW unc", v_lw, 1);
    }

    vec[0] = saved[0]; vec[1] = saved[1]; vec[2] = saved[2]; vec[3] = saved[3];
    bios_flush();
    write32(I_STAT, ~1u);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
