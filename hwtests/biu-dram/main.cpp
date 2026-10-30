/*
 * cpu/biu-dram - RAM_SIZE bit 7, "Delay on simultaneous CODE+DATA fetch from RAM (0=None, 1=One Cycle)", under cold loads and stores, alone and with the load scheduler off (2026-10-30)
 *
 * WHY. hwtests/biu-sched flipped FFFE0130h's switches for the first time, and with LDSCH off a cold RAM load reads 7.15 6.09 9.10 8.12 by word where it reads a flat 4.15 (the table in STATUS "A second opinion"). psx-spx names one more
 * switch on exactly that collision, 1F801060h bit 7, set in the BIOS's 00000B88h; STATUS had it down as "a retail line-fill test (list B)" since September and nothing ever cleared it. Cold loads here read 13-17 cycles a block too many and
 * cold stores the most off (load-fill2's STORE 5.18 against 5.66, LW+SW 11.44 against 12.19), so those rows are added.
 *
 * WHAT IT DOES. As biu-sched, one change at a time from uncached code, restored before each print:
 *   as the BIOS leaves it
 *   RAM_SIZE bit 7 clear
 *   LDSCH 0                       (biu-sched's, the control)
 *   LDSCH 0, RAM_SIZE bit 7 clear
 * the rows of biu-sched, and STORE (sw at word 0) and LW+SW (lw at word 0, sw at word 2), as load-fill2.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js biu-dram --frames=2500 --quiet=600`.
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
static uint32_t __attribute__((noinline, aligned(64))) storeB(uint32_t p) {
    uint32_t r;
    __asm__ volatile(HEAD ".rept 128\nsw $zero, 0(%1)\n.rept 15\nnop\n.endr\n.endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}
static uint32_t __attribute__((noinline, aligned(64))) lwsw(uint32_t p) {
    uint32_t r;
    __asm__ volatile(HEAD ".rept 128\nlw $t1, 0(%1)\nnop\nsw $zero, 4(%1)\n.rept 13\nnop\n.endr\n.endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}

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
static void __attribute__((noinline, aligned(64))) setCode(uint32_t v, uint32_t d) {
    __asm__ volatile(".set push\n.set noreorder\nsw %0, 0(%2)\nnop\nnop\nnop\nnop\nsw %1, 0(%3)\nnop\nnop\nnop\nnop\n.set pop\n" : : "r"(v), "r"(d), "r"(BIU_ADDR), "r"(0xBF801060u) : "memory");
}
static void setBoth(uint32_t v, uint32_t d) { ((void (*)(uint32_t, uint32_t))(((uint32_t)setCode & 0x1FFFFFFFu) | 0xA0000000u))(v, d); }
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
struct Rows { Span n, r[4], io, st, ls, wn, w, br[8]; };
static Rows rows;
static void measure(uint32_t ramA, uint32_t bufA) {
    static const Block lb[4] = { load0, load1, load2, load3 };
    static const Block br[8] = { l0, l1, l2, l3, l4, l7, t1c, t3c };
    rows.n = timeN(nops, 0, 4);
    for (int w = 0; w < 4; w++) rows.r[w] = timeN(lb[w], ramA, 4);
    rows.io = timeN(load1, 0xBF801070u, 4);
    rows.st = timeN(storeB, ramA, 4);
    rows.ls = timeN(lwsw, ramA, 4);
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
    line("STORE", rows.st, rows.n);
    line("LW+SW", rows.ls, rows.n);
    line("WARM NOPS", rows.wn, rows.wn);
    line("WARM RAM", rows.w, rows.wn);
    printf(" ");
    for (int k = 0; k < 8; k++) printf(" %s %u..%u;", bn[k], (unsigned)rows.br[k].lo, (unsigned)rows.br[k].hi);
    printf("\n\n");
}

int main() {
    printf("\ncpu/biu-dram\n");
    printf("RAM_SIZE bit 7 under cold and warm loads and stores (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    const uint32_t biu = getBiu();
    const uint32_t dram = *(volatile uint32_t*)0xBF801060u;
    printf("BIU as it stands: %08x, RAM_SIZE %08x\n\n", (unsigned)biu, (unsigned)dram);
    const uint32_t ramA = (uint32_t)&ramWord, bufA = (uint32_t)ram;
    struct { const char* name; uint32_t v, d; } mode[4] = {
        { "as the BIOS leaves it", biu, dram },
        { "RAM_SIZE bit 7 clear", biu, dram & ~0x80u },
        { "LDSCH 0", biu & ~0x10000u, dram },
        { "LDSCH 0, RAM_SIZE bit 7 clear", biu & ~0x10000u, dram & ~0x80u },
    };
    for (int m = 0; m < 4; m++) {
        printf("== %s: %08x %08x\n", mode[m].name, (unsigned)mode[m].v, (unsigned)mode[m].d);
        for (volatile int d = 0; d < 200000; d++) {}             /* that line on its way out before the bit moves: a hang names its bit */
        EnterCriticalSection();
        const uint16_t m2 = read16(T2_MODE);
        write16(T2_MODE, 0);
        setBoth(mode[m].v, mode[m].d);
        measure(ramA, bufA);
        setBoth(biu, dram);
        write16(T2_MODE, m2);
        ExitCriticalSection();
        print();
    }
    printf("cpu/biu-dram: Done.\n");
    for (;;) VSync(0);
}
