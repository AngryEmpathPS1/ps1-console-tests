/*
 * hwtests/exp-window - does an access past a region's SIZE FIELD raise a bus error? Expansion 2, 3 and 1.
 * (2026-11-17, the busValue session, for AngryEmpath's return; no console has run it yet.)
 *
 * WHY. The console fuzzer's case 90000 hung the SCPH-1001 and the SCPH-7502 where the emulator ran to Done
 * (experiments/fuzz-all/2026-11-16-cart/hang90000/FINDING.md); the bisect's lead is ONE load, `lb` from
 * 1F8025F5h, with no delay register touched. The kernel sets Expansion 2's Delay/Size (1F80101Ch) to 00070777h:
 * size field 7, a window of 128 bytes, 1F802000h-1F80207Fh. psx-spx (Memory Control): "Trying to access
 * addresses that exceed the selected size causes an exception ... The Size bits seem to be ignored for
 * SPU/CDROM." hwtests/bios-window measured exactly that for the BIOS ROM's window on three consoles
 * (v0.377.31). Nothing has measured it for Expansion 1, 2 or 3; the emulator decoded all of 1F802000h-1F802FFFh,
 * 1F000000h-1F7FFFFFh and 1FA00000h-1FBFFFFFh whatever the registers said, and the fuzz host installs no
 * handler, so a bus error there reads as a hang.
 *
 * WHAT. With its own vector at 80000080h (bios-window's: it keeps CAUSE, EPC and BadVaddr and returns to
 * EPC + 4, or to $ra when EPC is inside 1Fxxxxxxh - a fetch that faulted), for each region and each size:
 * lbu / lhu / lw (and, for Expansion 2 alone, sb) at offsets inside and past the window, each from the
 * I-cache, Timer 2 around it. A cell prints the value read or "BUSERR", the excode, where EPC fell (opcodes
 * past the access) and the cycles. Then, LAST and after the data rows are printed (so a hang there loses
 * nothing): a call into Expansion 2 and 3 through KSEG1, inside and past the window - a fetch's bus error (IBE).
 * FIRST of all, the fuzzer session's five cases one load a line, each line begun before its load: if a read past the
 * window is not a bus error but a read nothing acknowledges, the run stops behind that line, handler or not.
 * BadVaddr is printed in the table's summary only to show it is NOT written (psx-spx: bus errors leave it unchanged).
 *
 * SAFE ON A CONSOLE. Expansion 1 is LOADS ONLY (the hard rule: no store into 1F000000h-1F07FFFFh - a flash
 * command sequence could start a write; this test stores nowhere in 1F000000h-1F7FFFFFh and executes nothing
 * there). The only stores into a region are `sb` to Expansion 2, never at 1F802041h (POST). The three
 * Delay/Size registers get their SIZE FIELD changed (bits 16-20) and nothing else, are put back before
 * anything prints, and are read back "as found". Sizes stay at or under psx-spx's maxima (Expansion 1 17h,
 * Expansion 2 0Dh, Expansion 3 15h: "larger sizes would overlap the internal I/O ports, and crash the PSX").
 *
 * WHAT IS ALREADY KNOWN: ps1-tests' cpu/io-access-bitwidth reads a halfword and a word at 1F802000h and at
 * 1FA00000h (Expansion 3's window is ONE byte, 00003022h) on a console with no crash - so if the size field
 * counts, it is the access's first address that is judged. The size-0 / 1 / 2 rows here say so directly.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u

extern "C" {
volatile uint32_t xw_epc, xw_cause, xw_bad, xw_value;
void xw_vector(void);
uint32_t xw_lbu(uint32_t); uint32_t xw_lhu(uint32_t); uint32_t xw_lw(uint32_t); uint32_t xw_sb(uint32_t);
uint32_t xw_call(uint32_t);
uint32_t xw_lb(uint32_t); uint32_t xw_lh(uint32_t);
extern char xw_lbu_ld[], xw_lhu_ld[], xw_lw_ld[], xw_sb_ld[], xw_lb_ld[], xw_lh_ld[];
}

/* One access from the I-cache, Timer 2 before and after, eight no-ops of room for an exception that lands late. */
#define XW(NAME, OP) \
    ".balign 16\n" \
    ".global " #NAME "\n.global " #NAME "_ld\n" \
    #NAME ":\n" \
    "  lui   $t3, 0x1F80\n" \
    "  ori   $v1, $zero, 0x1EF7\n" \
    "  lhu   $t0, 0x1120($t3)\n" \
    #NAME "_ld:\n" \
    "  " OP "\n" \
    "  .rept 8\n  nop\n  .endr\n" \
    "  lhu   $t1, 0x1120($t3)\n" \
    "  lui   $t4, %hi(xw_value)\n" \
    "  sw    $v1, %lo(xw_value)($t4)\n" \
    "  subu  $v0, $t1, $t0\n" \
    "  jr    $ra\n" \
    "  andi  $v0, $v0, 0xFFFF\n"

__asm__(
    ".text\n"
    ".set push\n.set noreorder\n.set noat\n"
    /* Copied to 80000080h: four words, as the kernel's own vector. */
    ".balign 16\n"
    ".global xw_vector\n"
    "xw_vector:\n"
    "  lui   $k0, %hi(xw_handler)\n"
    "  addiu $k0, $k0, %lo(xw_handler)\n"
    "  jr    $k0\n"
    "  nop\n"
    /* CAUSE, BadVaddr and EPC kept; back to EPC + 4 (past the access), or to $ra when EPC is in 1Fxxxxxxh
     * (the call into a region: whatever exception the fetch or the fetched word raised). */
    ".global xw_handler\n"
    "xw_handler:\n"
    "  mfc0  $k1, $13\n"
    "  lui   $k0, %hi(xw_cause)\n"
    "  sw    $k1, %lo(xw_cause)($k0)\n"
    "  mfc0  $k1, $8\n"
    "  lui   $k0, %hi(xw_bad)\n"
    "  sw    $k1, %lo(xw_bad)($k0)\n"
    "  mfc0  $k1, $14\n"
    "  lui   $k0, %hi(xw_epc)\n"
    "  sw    $k1, %lo(xw_epc)($k0)\n"
    "  srl   $k0, $k1, 24\n"
    "  andi  $k0, $k0, 0x1F\n"
    "  xori  $k0, $k0, 0x1F\n"
    "  bne   $k0, $zero, 1f\n"
    "  addiu $k0, $k1, 4\n"
    "  move  $k0, $ra\n"
    "1:\n"
    "  jr    $k0\n"
    "  rfe\n"
    XW(xw_lbu, "lbu $v1, 0($a0)")
    XW(xw_lhu, "lhu $v1, 0($a0)")
    XW(xw_lw, "lw $v1, 0($a0)")
    XW(xw_sb, "sb $v1, 0($a0)")
    XW(xw_lb, "lb $v1, 0($a0)")
    XW(xw_lh, "lh $v1, 0($a0)")
    /* A call to a0: the handler comes back here through $ra. v0 = 1 if the callee itself returned (it cannot). */
    ".balign 16\n"
    ".global xw_call\n"
    "xw_call:\n"
    "  move  $t5, $ra\n"
    "  jalr  $a0\n"
    "  nop\n"
    "  nop\n"
    "  jr    $t5\n"
    "  nop\n"
    ".set pop\n");

typedef uint32_t (*Op)(uint32_t);

static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

#define NOP_ 4
static const Op ops[NOP_] = { xw_lbu, xw_lhu, xw_lw, xw_sb };
static const char *const opLd[NOP_] = { xw_lbu_ld, xw_lhu_ld, xw_lw_ld, xw_sb_ld };
static const char *const opName[NOP_] = { "lbu", "lhu", "lw", "sb" };
static const uint32_t opAlign[NOP_] = { 0, 1, 3, 0 };

#define MAXS 8
#define MAXO 12
struct Region {
    const char *name; uint32_t reg, base; int nSize, nOff, nOps;
    uint8_t sizes[MAXS]; uint32_t offs[MAXO];
};
/* The first size of each list is the kernel's own (7, 0, 13h); the last is psx-spx's maximum. */
static const Region regions[3] = {
    { "Expansion 2", 0x1F80101Cu, 0x1F802000u, 8, 11, 4,
      { 0x07, 0x00, 0x01, 0x02, 0x04, 0x08, 0x0C, 0x0D },
      { 0x000, 0x001, 0x002, 0x010, 0x07F, 0x080, 0x0FF, 0x100, 0x5F5, 0xFFF, 0x1000 } },
    { "Expansion 3", 0x1F80100Cu, 0x1FA00000u, 6, 10, 3,
      { 0x00, 0x01, 0x02, 0x08, 0x10, 0x15 },
      { 0x000000, 0x000001, 0x000002, 0x000004, 0x0000FF, 0x000100, 0x00FFFF, 0x010000, 0x1FFFFC, 0x000010 } },
    { "Expansion 1", 0x1F801008u, 0x1F000000u, 5, 10, 3,          /* LOADS ONLY: nOps 3 leaves sb out */
      { 0x13, 0x10, 0x12, 0x14, 0x17 },
      { 0x000000, 0x00FFFC, 0x010000, 0x03FFFC, 0x040000, 0x07FFFC, 0x080000, 0x0FFFFC, 0x100000, 0x7FFFFC } },
};

static uint32_t rVal[3][MAXS][MAXO][NOP_], rEpc[3][MAXS][MAXO][NOP_], rCause[3][MAXS][MAXO][NOP_], rBad[3][MAXS][MAXO][NOP_];
static uint16_t rCyc[3][MAXS][MAXO][NOP_];
static uint32_t rReg[3][MAXS], found[3], after[3];

static volatile uint32_t *const vec = (volatile uint32_t *)0x80000080u;
static uint32_t savedVec[4];
static uint16_t m2;
static void takeVector() {
    EnterCriticalSection();
    m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int i = 0; i < 4; i++) { savedVec[i] = vec[i]; vec[i] = ((const uint32_t *)xw_vector)[i]; }
    flushCache();
}
static void giveVector() {
    for (int i = 0; i < 4; i++) vec[i] = savedVec[i];
    flushCache();
    write16(T2_MODE, m2);
    ExitCriticalSection();
}

int main() {
    printf("\nbus/exp-window\n");
    printf("Loads (and, in Expansion 2, byte stores) inside and past each expansion region's window, at each size\n");
    printf("its Delay/Size register can be given; a handler in place so a bus error prints instead of hanging.\n");
    printf("There are no assertions - compare against your emulator.\n\n");

    for (int r = 0; r < 3; r++) found[r] = read32(regions[r].reg);
    for (int r = 0; r < 3; r++)
        printf("%s: %08x (Delay/Size) reads %08x as found, window size %02xh.\n", regions[r].name, (unsigned)regions[r].reg,
               (unsigned)found[r], (unsigned)(found[r] >> 16) & 0x1F);

    /* FIRST, THE FUZZER'S HANG (experiments/fuzz-all/2026-11-16-cart/hang90000: one `lb` from 1F8025F5h froze the SCPH-7502):
     * each case named BEFORE its load, so one run says (a) a bus error - the handler prints - or (b) a read nothing
     * acknowledges - the run stops behind a "before" line even with the handler in place. The fuzzer session's five cases. */
    printf("\nThe fuzzer's hang, one load a line, the line begun before the load:\n");
    static const struct { const char *what; Op op; const char *ld; uint32_t addr; uint8_t size; } hang[5] = {
        { "lb 1F80207Fh (the window's last byte)  ", xw_lb, xw_lb_ld, 0x1F80207Fu, 0xFF },
        { "lb 1F802080h (the first byte past it)  ", xw_lb, xw_lb_ld, 0x1F802080u, 0xFF },
        { "lb 1F8025F5h (the fuzzer's address)    ", xw_lb, xw_lb_ld, 0x1F8025F5u, 0xFF },
        { "lb 1F8025F5h with the size field at 0Dh", xw_lb, xw_lb_ld, 0x1F8025F5u, 0x0D },
        { "lh 1F802000h (a halfword in the window)", xw_lh, xw_lh_ld, 0x1F802000u, 0xFF },
    };
    for (int i = 0; i < 5; i++) {
        printf("  before %s: ", hang[i].what);
        takeVector();
        if (hang[i].size != 0xFF) write32(regions[0].reg, (found[0] & ~0x001F0000u) | ((uint32_t)hang[i].size << 16));
        hang[i].op(hang[i].addr);
        xw_epc = 0; xw_cause = 0; xw_value = 0;
        const uint32_t cyc = hang[i].op(hang[i].addr), e = xw_epc, c = xw_cause, v = xw_value;
        write32(regions[0].reg, found[0]);
        giveVector();
        if (e) printf("BUS ERROR  excode %02u  EPC %08x (the load at %08x)  CAUSE %08x  %u cycles\n", (unsigned)(c >> 2) & 31,
                      (unsigned)e, (unsigned)(uint32_t)hang[i].ld, (unsigned)(c & ~0xFF00u), (unsigned)cyc);
        else printf("answered %08x  %u cycles\n", (unsigned)v, (unsigned)cyc);
    }
    printf("\nThe table (a row of sizes at a time; a region's name is printed before its loads):\n");

    for (int r = 0; r < 3; r++) {
        const Region &R = regions[r];
        if (r == 2 && R.nOps != 3) { printf("Expansion 1 must be loads only: refused\n"); for (;;) {} }
        printf("  %s ...\n", R.name);
        takeVector();
        for (int s = 0; s < R.nSize; s++) {
            write32(R.reg, (found[r] & ~0x001F0000u) | ((uint32_t)R.sizes[s] << 16));
            rReg[r][s] = read32(R.reg);
            for (int o = 0; o < R.nOff; o++)
                for (int k = 0; k < R.nOps; k++) {
                    const uint32_t a = R.base + (R.offs[o] & ~opAlign[k]);
                    if (k == 3 && a == 0x1F802041u) continue;              /* POST: never stored to */
                    ops[k](a);                                             /* the second call runs from the I-cache */
                    xw_epc = 0; xw_cause = 0; xw_bad = 0; xw_value = 0;
                    rCyc[r][s][o][k] = (uint16_t)ops[k](a);
                    rVal[r][s][o][k] = xw_value; rEpc[r][s][o][k] = xw_epc; rCause[r][s][o][k] = xw_cause; rBad[r][s][o][k] = xw_bad;
                }
        }
        write32(R.reg, found[r]);
        after[r] = read32(R.reg);
        giveVector();
    }

    for (int r = 0; r < 3; r++) {
        const Region &R = regions[r];
        printf("%s: %08x (Delay/Size) read %08x as found, %08x after the test.\n", R.name,
               (unsigned)R.reg, (unsigned)found[r], (unsigned)after[r]);
    }
    printf("\nA cell: the value read (sb: '-'), or BUSERR e<excode> +<EPC, opcodes past the access>; then the cycles.\n");
    for (int r = 0; r < 3; r++) {
        const Region &R = regions[r];
        uint32_t aCause = 0, aBad = 0, aAddr = 0; int any = 0;
        for (int s = 0; s < R.nSize; s++) {
            printf("\n%s, size %02xh (%u bytes), register reads %08x:\n", R.name, R.sizes[s], 1u << R.sizes[s], (unsigned)rReg[r][s]);
            printf("  address  ");
            for (int k = 0; k < R.nOps; k++) printf(" %-21s", opName[k]);
            printf("\n");
            for (int o = 0; o < R.nOff; o++) {
                printf("  %08x ", (unsigned)(R.base + R.offs[o]));
                for (int k = 0; k < R.nOps; k++) {
                    if (rEpc[r][s][o][k]) {
                        const int past = ((int)(rEpc[r][s][o][k] & 0x1FFFFFFFu) - (int)((uint32_t)opLd[k] & 0x1FFFFFFFu)) / 4;
                        printf(" BUSERR e%02u %+d  %5u  ", (unsigned)(rCause[r][s][o][k] >> 2) & 31, past, rCyc[r][s][o][k]);
                        if (!any) { any = 1; aCause = rCause[r][s][o][k]; aBad = rBad[r][s][o][k]; aAddr = R.base + (R.offs[o] & ~opAlign[k]); }
                    } else if (k == 3) printf(" -              %5u  ", rCyc[r][s][o][k]);
                    else printf(" %08x       %5u  ", (unsigned)rVal[r][s][o][k], rCyc[r][s][o][k]);
                }
                printf("\n");
            }
        }
        if (any) printf("\n%s, its first bus error (at %08x): CAUSE %08x (the pending-interrupt bits masked), BadVaddr %08x\n",
                        R.name, (unsigned)aAddr, (unsigned)(aCause & ~0xFF00u), (unsigned)aBad);
        else printf("\n%s: no bus error at any size or address.\n", R.name);
    }

    /* LAST: a fetch. Expansion 2 and 3 only (nothing is ever executed from Expansion 1), through KSEG1, registers as found. */
    printf("\nA call into a region (KSEG1), the registers as found: the exception the fetch or the word fetched raises.\n");
    static const uint32_t callAt[6] = { 0xBF802000u, 0xBF802080u, 0xBF8025F4u, 0xBFA00000u, 0xBFA00004u, 0xBFA10000u };
    for (int i = 0; i < 6; i++) {
        printf("  call %08x: ", (unsigned)callAt[i]);           /* printed first: a hang here names its row */
        takeVector();
        xw_epc = 0; xw_cause = 0; xw_bad = 0;
        xw_call(callAt[i]);
        const uint32_t e = xw_epc, c = xw_cause, b = xw_bad;
        giveVector();
        if (e) printf("excode %02u  EPC %08x  CAUSE %08x  BadVaddr %08x\n", (unsigned)(c >> 2) & 31, (unsigned)e, (unsigned)(c & ~0xFF00u), (unsigned)b);
        else printf("no exception\n");
    }

    printf("\nbus/exp-window: Done.\n");
    for (;;) VSync(0);
}
