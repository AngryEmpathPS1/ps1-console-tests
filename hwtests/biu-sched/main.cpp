/*
 * cpu/biu-sched - the CPU's own switches: FFFE0130h's load scheduling, streaming and refill size, one at a time, under the loads this core still cannot explain (2026-10-30)
 *
 * WHY. Every load-shadow and streaming rule in this core was fitted from outside, with the BIU as the BIOS leaves it (0001E988h). LSI's manual (L64360 ch.14) names switches for them. psx-spx,
 * read for this after a second opinion (an AI's report, not included), gives bit 16 LDSCH (a load overlaps the independent instructions behind it, off
 * about 2 cycles dearer), bit 17 NOSTR (streaming off: uncached fetch a cycle per two instructions dearer), bits 8-9 IBLKSZ (1 four-word refill, 0 two-word). No program here ever toggled them. Turned off,
 * they say which part of what we measured IS that mechanism: the cold RAM load's flat 4.15 (load-fill), T1's 24 against 23 (refresh-store3).
 * pcsx-redux's tests/dcache/probe.s says RDPRI, NOPAD, BGNT and LDSCH "must be preserved or the bus hangs", where psx-spx toggled each. So this flips ONE bit at a time, from uncached code, for
 * one measurement, and restores it before printing; each mode prints a line BEFORE it starts, so a hang names its bit. Someone has to be at the console for this run: a hang needs RESET.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each mode, BIU value in hand:
 *   as the BIOS leaves it   the control
 *   IBLKSZ 0                two-word refill
 *   NOSTR 1                 no streaming
 *   LDSCH 0                 no load scheduling
 * the same rows: 8 KB of cold straight-line code as hwtests/load-fill (NOPS, a RAM load at word 0..3, an I_STAT load at word 1), its WARM loop, and warm brackets, 64 trials' fastest..slowest:
 *   lw; k nops               k = 0, 1, 2, 3, 4, 7       the load's overlap
 *   T1, T3                   sw A; lhu T2; sw A+8; [nop;] lw A+8
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js biu-sched --frames=2500 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define BIU_ADDR 0xFFFE0130u

uint32_t ramWord;
static volatile uint32_t ram[64] __attribute__((aligned(4096)));
typedef uint32_t (*Block)(uint32_t);

#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"
#define UNIT(W) ".rept " #W "\nnop\n.endr\nlw $t1, 0(%1)\n.rept 15-" #W "\nnop\n.endr\n"
#define LOADBLOCK(NAME, W) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT(W) ".endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }
static uint32_t __attribute__((noinline, aligned(64))) nops(uint32_t) {
    uint32_t r;
    __asm__ volatile(HEAD ".rept 2048\nnop\n.endr\n" TAIL : "=&r"(r) : : "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}
LOADBLOCK(load0, 0) LOADBLOCK(load1, 1) LOADBLOCK(load2, 2) LOADBLOCK(load3, 3)

static uint32_t __attribute__((noinline, aligned(64))) warm(uint32_t p) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nli $t6, 128\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
                     "1:\nlw $t1, 0(%1)\n.rept 12\nnop\n.endr\naddiu $t6, $t6, -1\nbnez $t6, 1b\nnop\n" TAIL
                     : "=&r"(r) : "r"(p) : "t1", "t6", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}
static uint32_t __attribute__((noinline, aligned(64))) warmNops(uint32_t) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nli $t6, 128\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
                     "1:\nnop\n.rept 12\nnop\n.endr\naddiu $t6, $t6, -1\nbnez $t6, 1b\nnop\n" TAIL
                     : "=&r"(r) : : "t6", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}

/* warm brackets: lhu T2; body; lhu T2 */
#define CASE(NAME, BODY) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t4, 0x1f80\nlhu $t1, 0x1120($t4)\n" BODY "lhu $t3, 0x1120($t4)\nnop\nsubu %0, $t3, $t1\n.set pop\n" \
                     : "=&r"(r) : "r"(p) : "t1", "t2", "t3", "t4", "memory"); \
    return r & 0xFFFF; }
CASE(l0, "lw $t2, 0(%1)\n")
CASE(l1, "lw $t2, 0(%1)\nnop\n")
CASE(l2, "lw $t2, 0(%1)\nnop\nnop\n")
CASE(l3, "lw $t2, 0(%1)\nnop\nnop\nnop\n")
CASE(l4, "lw $t2, 0(%1)\nnop\nnop\nnop\nnop\n")
CASE(l7, "lw $t2, 0(%1)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n")
CASE(t1c, "sw $zero, 0(%1)\nlhu $t2, 0x1120($t4)\nsw $zero, 8(%1)\nlw $t2, 8(%1)\n")
CASE(t3c, "sw $zero, 0(%1)\nlhu $t2, 0x1120($t4)\nsw $zero, 8(%1)\nnop\nlw $t2, 8(%1)\n")

/* From uncached code, the BIU written, four nops; nothing cached runs across the write. */
static void __attribute__((noinline, aligned(64))) setBiuCode(uint32_t v) {
    __asm__ volatile(".set push\n.set noreorder\nsw %0, 0(%1)\nnop\nnop\nnop\nnop\n.set pop\n" : : "r"(v), "r"(BIU_ADDR) : "memory");
}
static void setBiu(uint32_t v) { ((void (*)(uint32_t))(((uint32_t)setBiuCode & 0x1FFFFFFFu) | 0xA0000000u))(v); }
static uint32_t getBiu() { return *(volatile uint32_t*)BIU_ADDR; }

struct Span { uint32_t lo, hi; };
static Span timeN(Block f, uint32_t p, int n) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < n; k++) {
        const uint32_t t = f(p);
        if (t < s.lo) s.lo = t;
        if (t > s.hi) s.hi = t;
    }
    return s;
}

/* One mode's numbers, taken with the bit changed, printed after it is back. */
struct Rows { Span n, r[4], io, wn, w, br[8]; };
static Rows rows;
static void measure(uint32_t ramA, uint32_t bufA) {
    static const Block lb[4] = { load0, load1, load2, load3 };
    static const Block br[8] = { l0, l1, l2, l3, l4, l7, t1c, t3c };
    rows.n = timeN(nops, 0, 4);
    for (int w = 0; w < 4; w++) rows.r[w] = timeN(lb[w], ramA, 4);
    rows.io = timeN(load1, 0xBF801070u, 4);
    rows.wn = timeN(warmNops, 0, 4);
    rows.w = timeN(warm, ramA, 4);
    for (int k = 0; k < 8; k++) { br[k](bufA); rows.br[k] = timeN(br[k], bufA, 64); }
}
static void line(const char* name, Span s, Span base) {
    const int d = (int)s.lo - (int)base.lo;
    const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
    printf("  %-12s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a load\n", name, (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
}
static void print() {
    static const char* const rn[4] = { "RAM w=0", "RAM w=1", "RAM w=2", "RAM w=3" };
    static const char* const bn[8] = { "lw", "lw; 1 nop", "lw; 2 nops", "lw; 3 nops", "lw; 4 nops", "lw; 7 nops", "T1", "T3" };
    line("NOPS", rows.n, rows.n);
    for (int w = 0; w < 4; w++) line(rn[w], rows.r[w], rows.n);
    line("I_STAT w=1", rows.io, rows.n);
    line("WARM NOPS", rows.wn, rows.wn);
    line("WARM RAM", rows.w, rows.wn);
    printf(" ");
    for (int k = 0; k < 8; k++) printf(" %s %u..%u;", bn[k], (unsigned)rows.br[k].lo, (unsigned)rows.br[k].hi);
    printf("\n\n");
}

int main() {
    printf("\ncpu/biu-sched\n");
    printf("FFFE0130h's switches one at a time, under cold and warm loads (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    const uint32_t biu = getBiu();
    printf("BIU as it stands: %08x\n\n", (unsigned)biu);
    const uint32_t ramA = (uint32_t)&ramWord, bufA = (uint32_t)ram;
    struct { const char* name; uint32_t v; } mode[4] = {
        { "as the BIOS leaves it", biu },
        { "IBLKSZ 0 (two-word refill)", biu & ~0x300u },
        { "NOSTR 1 (no streaming)", biu | 0x20000u },
        { "LDSCH 0 (no load scheduling)", biu & ~0x10000u },
    };
    for (int m = 0; m < 4; m++) {
        printf("== %s: %08x\n", mode[m].name, (unsigned)mode[m].v);
        for (volatile int d = 0; d < 200000; d++) {}             /* that line on its way out before the bit moves: a hang names its bit */
        EnterCriticalSection();
        const uint16_t m2 = read16(T2_MODE);
        write16(T2_MODE, 0);
        setBiu(mode[m].v);
        measure(ramA, bufA);
        setBiu(biu);
        write16(T2_MODE, m2);
        ExitCriticalSection();
        print();
    }
    printf("cpu/biu-sched: Done.\n");
    for (;;) VSync(0);
}
