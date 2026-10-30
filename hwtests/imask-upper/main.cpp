/*
 * cpu/imask-upper - what fills the top half of a 32-bit read of a 16-bit
 * register?
 *
 * WHY (ROAD A8 "I_MASK's upper bits", dug 2026-09-27; ROAD B). I_MASK
 * (1F801074h) holds bits 0-10 (bits 11-15 read 0 - psx-spx, DIAG's write test
 * at 7FFh, MiSTer; settled). A 32-bit read's bits 16-31 hold no state, and
 * their VALUE is open, because two consoles disagree:
 *   ps1-tests cpu/io-access-bitwidth reads 12340678h at 1F801074h straight
 *   after writing 12345678h there - the last data on the bus;
 *   pcsx-redux nugget regwrites, on an SCPH-5501, reads BF800xxxh at
 *   BF801074h - the top half of the ADDRESS.
 * And io-access-bitwidth's one 32-bit read of T0_TARGET gave 3C045678h, whose
 * top half is a `lui a0` - an instruction fetch on the bus. This emulator
 * returns the last value written to any I/O register (SIO's ioBusLatch) and
 * nothing else, which is why it misses that cell.
 *
 * WHAT IT DOES. Interrupts off. I_MASK is read with one lw straight after one
 * of five things, each with its own marker in the top half:
 *   sw I_MASK      a store of A5A5xxxxh to I_MASK itself (the mask kept)
 *   sw T2 target   a store of 5A5Axxxxh to Timer 2's target register
 *   lw RAM         a load of C3C3xxxxh from main RAM (no data cache: the bus)
 *   lw scratch     a load of E7E7xxxxh from the scratchpad (on the chip)
 *   nothing        only the instructions around it
 * through three segments - KUSEG 1F801074h, KSEG1 BF801074h, KSEG0 9F801074h -
 * and from code run CACHED (its second call: the lines already in the
 * I-cache) and UNCACHED (through its KSEG1 alias, every fetch on the bus).
 * The instructions around each read write only $zero and have their own top
 * halves, so a fetch that lands in the latch names its place:
 *   3000h two before the read, 3800h the one before, 8Cxx-8Dxx the read itself,
 *   3400h one after, 2400h two after, 2800h three after, 2C00h four after.
 *
 * READ IT LIKE THIS. The top half of each cell is the answer: A5A5 or 5A5A the
 * last store's data; C3C3 the last load's data; E7E7 would mean the scratchpad
 * shares the bus (it should not); 1F80 / BF80 / 9F80 the address (the three
 * segments then differ); one of the instruction halves a fetch. The bottom
 * half is I_MASK itself and must not change. Say which console model ran it:
 * the two logs above came from different ones.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js imask-upper`): the last I/O
 * STORE's data, and nothing else. A5A5 after the I_MASK store, 5A5A after the
 * Timer 2 store - and 5A5A still after the RAM load, the scratchpad load and
 * nothing (the store from the case before), in all three segments, cached
 * and uncached. No address echo, no fetch. A console that echoes the address
 * reads 1F80/BF80/9F80 across the three columns; one whose latch keeps read
 * data reads C3C3 in the RAM row; one that latches fetches reads an
 * instruction half in the uncached rows.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_MASK_KUSEG 0x1F801074u
#define T2_TARGET    0x1F801128u

static volatile uint32_t ramWord = 0xC3C35A00u;
#define SCRATCH_WORD 0x1F800100u

/* The five cases. Each reads `rd` once, as the fourth instruction of a fixed sequence; `a`/`b` are its operands.
 * `.set noreorder` keeps the order exactly as written, the filler instructions included. */
#define FILLER_BEFORE "andi $0, $0, 0x1111\n xori $0, $0, 0x2222\n"
#define FILLER_AFTER  "ori $0, $0, 0x3333\n addiu $0, $0, 0x4444\n slti $0, $0, 0x5555\n sltiu $0, $0, 0x6666\n"

static uint32_t __attribute__((noinline, aligned(64))) caseStore(uint32_t a, uint32_t b, uint32_t rd) {
    uint32_t r;
    __asm__ volatile(".set noreorder\n sw %2, 0(%1)\n" FILLER_BEFORE " lw %0, 0(%3)\n" FILLER_AFTER " .set reorder\n"
                     : "=&r"(r) : "r"(a), "r"(b), "r"(rd) : "memory");
    return r;
}
static uint32_t __attribute__((noinline, aligned(64))) caseLoad(uint32_t a, uint32_t b, uint32_t rd) {
    uint32_t r, t;
    (void)b;
    __asm__ volatile(".set noreorder\n lw %1, 0(%2)\n" FILLER_BEFORE " lw %0, 0(%3)\n" FILLER_AFTER " .set reorder\n"
                     : "=&r"(r), "=&r"(t) : "r"(a), "r"(rd) : "memory");
    return r;
}
static uint32_t __attribute__((noinline, aligned(64))) caseNothing(uint32_t a, uint32_t b, uint32_t rd) {
    uint32_t r;
    (void)a; (void)b;
    __asm__ volatile(".set noreorder\n nop\n" FILLER_BEFORE " lw %0, 0(%1)\n" FILLER_AFTER " .set reorder\n"
                     : "=&r"(r) : "r"(rd) : "memory");
    return r;
}

typedef uint32_t (*CaseFn)(uint32_t, uint32_t, uint32_t);
static CaseFn uncached(CaseFn f) { return (CaseFn)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

int main() {
    printf("\ncpu/imask-upper\n");
    printf("A 32-bit read of I_MASK: what fills bits 16-31, after a store, a load, or nothing?\n");
    printf("There are no assertions - compare against your emulator, and say which console model ran it.\n\n");
    EnterCriticalSection();
    const uint32_t mask = read32(I_MASK_KUSEG) & 0x7FF;
    const uint32_t t2 = read32(T2_TARGET) & 0xFFFF;
    write32(SCRATCH_WORD, 0xE7E75A00u);
    const uint32_t segs[3] = { 0x1F801074u, 0xBF801074u, 0x9F801074u };
    const char* rows[5] = { "sw I_MASK    A5A5", "sw T2 target 5A5A", "lw RAM       C3C3", "lw scratch   E7E7", "nothing          " };
    uint32_t v[2][5][3];
    for (int u = 0; u < 2; u++) {
        for (int s = 0; s < 3; s++) {
            const uint32_t rd = segs[s];
            for (int rep = 0; rep < 2; rep++) {           /* the second call is the one kept: its lines are cached */
                CaseFn st = u ? uncached(caseStore) : caseStore;
                CaseFn ld = u ? uncached(caseLoad) : caseLoad;
                CaseFn no = u ? uncached(caseNothing) : caseNothing;
                v[u][0][s] = st(rd, 0xA5A50000u | mask, rd);
                v[u][1][s] = st(T2_TARGET, 0x5A5A0000u | t2, rd);
                v[u][2][s] = ld((uint32_t)&ramWord, 0, rd);
                v[u][3][s] = ld(SCRATCH_WORD, 0, rd);
                v[u][4][s] = no(0, 0, rd);
            }
        }
    }
    write32(I_MASK_KUSEG, mask);
    write32(T2_TARGET, t2);
    ExitCriticalSection();
    printf("I_MASK itself: %03x\n\n", (unsigned)mask);
    for (int u = 0; u < 2; u++) {
        printf("%s code            KUSEG 1F801074  KSEG1 BF801074  KSEG0 9F801074\n", u ? "uncached" : "cached  ");
        for (int r = 0; r < 5; r++)
            printf("  after %s    %08x        %08x        %08x\n", rows[r], (unsigned)v[u][r][0], (unsigned)v[u][r][1], (unsigned)v[u][r][2]);
        printf("\n");
    }
    printf("Top halves: A5A5/5A5A a store's data, C3C3 a load's, E7E7 the scratchpad, 1F80/BF80/9F80 the address,\n");
    printf("3000 3800 = fetches two and one before the read, 8Cxx/8Dxx the read, 3400 2400 2800 2C00 one to four after.\n");
    printf("\ncpu/imask-upper: Done.\n");
    for (;;) VSync(0);
}
