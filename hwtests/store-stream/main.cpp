/*
 * cpu/store-stream - what a stream of stores costs cold code at every phase of the DRAM refresh (2026-10-31)
 *
 * WHY. Every cold-code test that puts one store in a stream reads high here: hwtests/biu-dram's STORE 5.73 a unit against 5.21,
 * LW+SW 12.19 against 11.47, and the console's row spreads over 44 cycles in four samples where this core's spreads over 2. A lone
 * store is exact: hwtests/refresh-cold's S1 and SS match at all 256 phases. So what is missing belongs to the stream. Its unit
 * is 42.6 cycles, six of them 255.6, nearly the refresh's 256: every refresh of a block lands at about the same place in a unit,
 * and the block's start phase picks that place. biu-dram's four samples cannot show it, so this maps it.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, free-running. CALIBRATION: `lhu T2; lw RAM; lhu T2`, warm: F = the first
 * phase of its highest cost, as hwtests/refresh-cold. Then each case: 128 units of 16 words, 8 KB, twice the instruction cache, so
 * that every line misses every time, with what the case puts in each unit. A trial calls the block; the block reads Timer 2 at
 * its start and end. The cost is printed by x, the block's start phase after F, at each of the 256 phases, the lowest seen there,
 * and the number of phases where two trials disagreed.
 *   N    nops only
 *   L0   lw RAM at word 0
 *   L1   lw RAM at word 1
 *   S0..S3  sw RAM at that word
 *   LS   lw RAM at word 0, sw RAM at word 2 (biu-dram's LW+SW)
 * The whole lot twice: as the BIOS leaves RAM_SIZE, and with bit 7 clear, where the console's STORE row spreads over 3.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js store-stream --frames=2500 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define TRIALS 6000
#define NC 8

uint32_t ramWord;
typedef uint32_t (*Block)(uint32_t);

#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\nandi %0, %0, 0xffff\nsll $t8, $t8, 16\nor %0, %0, $t8\n.set pop\n"
#define BLOCK(NAME, UNIT) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT ".endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory"); \
    return r; }
#define NOPS(N) ".rept " #N "\nnop\n.endr\n"
BLOCK(bN, NOPS(16))
BLOCK(bL0, "lw $t1, 0(%1)\n" NOPS(15))
BLOCK(bL1, "nop\nlw $t1, 0(%1)\n" NOPS(14))
BLOCK(bS0, "sw $zero, 0(%1)\n" NOPS(15))
BLOCK(bS1, "nop\nsw $zero, 0(%1)\n" NOPS(14))
BLOCK(bS2, NOPS(2) "sw $zero, 0(%1)\n" NOPS(13))
BLOCK(bS3, NOPS(3) "sw $zero, 0(%1)\n" NOPS(12))
BLOCK(bLS, "lw $t1, 0(%1)\nnop\nsw $zero, 4(%1)\n" NOPS(13))

static uint32_t __attribute__((noinline, aligned(64))) cal(uint32_t p) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nlhu $t8, 0x1120($t9)\nlw $t1, 0(%1)\nlhu $t7, 0x1120($t9)\n"
                     "nop\nsubu %0, $t7, $t8\nandi %0, %0, 0xffff\nsll $t8, $t8, 16\nor %0, %0, $t8\n.set pop\n"
                     : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory");
    return r;
}

/* From uncached code, the register written, four nops; nothing cached runs across the write (hwtests/biu-dram). */
static void __attribute__((noinline, aligned(64))) setCode(uint32_t d) {
    __asm__ volatile(".set push\n.set noreorder\nsw %0, 0(%1)\nnop\nnop\nnop\nnop\n.set pop\n" : : "r"(d), "r"(0xBF801060u) : "memory");
}
static void setDram(uint32_t d) { ((void (*)(uint32_t))(((uint32_t)setCode & 0x1FFFFFFFu) | 0xA0000000u))(d); }

static uint16_t lo[256], hi[256];
static int F;
static void sweep(Block f, uint32_t p, int trials, int base) {
    for (int i = 0; i < 256; i++) { lo[i] = 0xFFFF; hi[i] = 0; }
    for (int t = 0; t < trials; t++) {
        for (volatile int d = 0; d < ((t * 7) & 31); d++) {}
        switch ((t >> 2) & 3) {                 /* an odd cycle or two: the loop above alone lands on half the phases */
            case 3: __asm__ volatile("nop");    /* fall through */
            case 2: __asm__ volatile("nop");    /* fall through */
            case 1: __asm__ volatile("nop");    /* fall through */
            default: break;
        }
        const uint32_t r = f(p);
        const int x = (int)((r >> 16) - base) & 255;
        const uint16_t c = (uint16_t)r;
        if (c < lo[x]) lo[x] = c;
        if (c > hi[x]) hi[x] = c;
    }
}

static const Block BLK[NC] = { bN, bL0, bL1, bS0, bS1, bS2, bS3, bLS };
static const char* const NAME[NC] = { "N", "L0", "L1", "S0", "S1", "S2", "S3", "LS" };
static uint16_t keep[NC][256], wide[NC], seen[NC];

/* Under nugget's monitor the loop at the end of main leaves it deaf: it reads the link for STOP only when an interrupt is taken, and
 * nothing here unmasks one (the SCPH-5501 #2, 2026-10-31: a RESET). The monitor copies the 80h vector to 40h; nothing else does. */
static void leaveToMonitor() {
    const volatile uint32_t* v = (const volatile uint32_t*)0x80000040u;
    if (v[0] != 0 && v[0] == v[16] && v[1] == v[17]) __asm__ volatile("break 4, 0");
}

int main() {
    printf("\ncpu/store-stream\n");
    printf("A stream of stores in cold code at every phase of the refresh (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    const uint32_t dram = *(volatile uint32_t*)0xBF801060u;
    const uint32_t p = (uint32_t)&ramWord;
    printf("RAM_SIZE %08x, the word at %08x\n\n", (unsigned)dram, (unsigned)p);
    /* Timer 2 is set once: a write to its mode restarts the count, and F is a phase of the count. */
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    sweep(cal, p, 60000, 0);
    int top = -1;
    F = 0;
    for (int i = 0; i < 256; i++) if (lo[i] != 0xFFFF && (int)lo[i] > top) top = lo[i];
    for (int i = 0; i < 256; i++) if (lo[i] == top && lo[(i + 255) & 255] != top) { F = i; break; }
    const int calLow = lo[(F + 128) & 255], calTop = top;
    ExitCriticalSection();
    for (int m = 0; m < 2; m++) {
        for (volatile int d = 0; d < 200000; d++) {}
        EnterCriticalSection();
        const uint32_t d = m ? dram & ~0x80u : dram;
        setDram(d);
        for (int c = 0; c < NC; c++) {
            sweep(BLK[c], p, TRIALS, F);
            wide[c] = 0; seen[c] = 0;
            for (int i = 0; i < 256; i++) {
                keep[c][i] = lo[i];
                if (lo[i] != 0xFFFF) seen[c]++;
                if (lo[i] != 0xFFFF && hi[i] != lo[i]) wide[c]++;
            }
        }
        setDram(dram);
        ExitCriticalSection();
        printf("== %s: RAM_SIZE %08x; calibration %d, highest %d first at phase %d (F)\n", m ? "bit 7 clear" : "as the BIOS leaves it", (unsigned)d, calLow, calTop, F);
        for (int c = 0; c < NC; c++) {
            int a = 0xFFFF, b = 0;
            for (int i = 0; i < 256; i++) if (keep[c][i] != 0xFFFF) { if (keep[c][i] < a) a = keep[c][i]; if (keep[c][i] > b) b = keep[c][i]; }
            printf("%s  lowest %d highest %d, %d of 256 phases seen, %d where two trials differed\n", NAME[c], a, b, seen[c], wide[c]);
            for (int x = 0; x < 256; x++) {
                if (keep[c][x] == 0xFFFF) printf("    .");
                else printf(" %4d", keep[c][x]);
                if (x % 16 == 15) printf("\n");
            }
        }
        printf("\n");
    }
    write16(T2_MODE, m2);
    printf("cpu/store-stream: Done.\n");
    leaveToMonitor();
    for (;;) VSync(0);
}
