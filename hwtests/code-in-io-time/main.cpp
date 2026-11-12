/*
 * cpu/code-in-io-time - what an instruction fetched from an on-die register costs: MEM_IO, timed.
 *
 * WHY (docs/INSTRUMENT-AUDIT.md flag #16, 2026-11-12). src/psx.js MEM_IO 2 - the stall of an instruction FETCH from I/O - is
 * cited to ps1-tests' cpu/code-in-io, a pass / fail test with no timer anywhere in its source (seven `jr $ra; nop` calls into
 * RAM, the scratchpad, the MDEC, I_STAT, the SPU and two DMA registers). Nothing measured the 2. hwtests/code-in-io-safe showed
 * which cases come back clean on every console from the SCPH-5501 on: testCodeInDMA0 (the code at BCR0, 1F801084h, and CHCR0
 * read back as written, no exception) - so the DMA0 registers are the place to time it.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. The code at the DMA0 registers: MADR0 (1F801080h) = 0 (a nop: the
 * register keeps 24 bits), BCR0 = 03E00008h (jr $ra), CHCR0 = 0 (a nop, nothing starts). Entered at BCR0 it is two I/O
 * fetches (jr, its delay slot); entered at MADR0, three. From cached code, 64 x `jalr; nop` into it, Timer 2 read around the
 * 64, at eight phases (a nop sled of 0..7 before the first read), each phase twice (the second kept: the caller warm), four
 * passes; fastest and slowest of the 32 a case, per call. The same calls into a two- and a three-word stub in RAM, cached and
 * through KSEG1 (an uncached RAM fetch is MEM_RAM_FETCH 4 here, M160: the instrument checked against a known number), and an
 * empty bracket (64 x two nops). The I/O code through KUSEG (1F80xxxxh), KSEG0 (9F80xxxxh) and KSEG1 (BF80xxxxh): whether the
 * cache controller fills a line from I/O shows as the cached segments' calls costing what a cache hit does after the first.
 * And the first call alone after FlushCache, per segment.
 *
 * READ IT LIKE THIS. "per I/O fetch" = (MADR0 entry - BCR0 entry): one more fetch from I/O, a whole fetch's cost at the
 * instruction's place in the pipe (an on-die fetch here costs 1 + MEM_IO). The same difference in uncached RAM is one RAM fetch
 * (1 + MEM_RAM_FETCH, six apart with the turnaround: M160's). If the KUSEG / KSEG0 rows read as the RAM cached rows, the
 * I-cache caches I/O code.
 *
 * The SCPH-1000 (PU-7) runs no code out of I/O (STATUS "The SCPH-1000 runs no code out of I/O": code-in-io-safe froze it; its
 * DMA0 case unseen): run this there last, or not. Every wait here is bounded; nothing waits on a device.
 *
 * THE RULE: run it here first: `node hwtests/run.js code-in-io-time`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define MADR0 0x1F801080u
#define BCR0  0x1F801084u
#define CHCR0 0x1F801088u

extern "C" {
extern uint32_t ramStub2[], ramStub3[];
void flushCache(void);
}
__asm__(".set push\n.set noreorder\n"
        ".balign 16\n.global ramStub2\nramStub2:\n  jr $ra\n  nop\n  nop\n  nop\n"
        ".balign 16\n.global ramStub3\nramStub3:\n  nop\n  jr $ra\n  nop\n  nop\n"
        ".global flushCache\nflushCache:\n  li $t1, 0x44\n  li $t2, 0xa0\n  jr $t2\n  nop\n"
        ".set pop\n");

/* 64 calls of `target`, a sled of `sled` nops (0..7) before the first Timer 2 read. calls == 0: the empty bracket. */
static uint32_t __attribute__((noinline, aligned(64))) run64(uint32_t target, uint32_t sled, int calls) {
    uint32_t t;
    if (calls) {
        __asm__ volatile(
            ".set push\n.set noreorder\n"
            " lui $t9, 0x1f80\n"
            " la $t8, 9f\n sll $t7, %[sl], 2\n subu $t8, $t8, $t7\n jr $t8\n nop\n"
            " .rept 8\n nop\n .endr\n"
            "9:\n lhu $t0, 0x1120($t9)\n nop\n"
            " .rept 64\n jalr %[tg]\n nop\n .endr\n"
            " lhu $t1, 0x1120($t9)\n nop\n"
            " subu %[t], $t1, $t0\n andi %[t], %[t], 0xffff\n"
            ".set pop\n"
            : [t] "=&r"(t) : [tg] "r"(target), [sl] "r"(sled) : "t0", "t1", "t7", "t8", "t9", "ra", "memory");
    } else {
        __asm__ volatile(
            ".set push\n.set noreorder\n"
            " lui $t9, 0x1f80\n"
            " la $t8, 9f\n sll $t7, %[sl], 2\n subu $t8, $t8, $t7\n jr $t8\n nop\n"
            " .rept 8\n nop\n .endr\n"
            "9:\n lhu $t0, 0x1120($t9)\n nop\n"
            " .rept 64\n nop\n nop\n .endr\n"
            " lhu $t1, 0x1120($t9)\n nop\n"
            " subu %[t], $t1, $t0\n andi %[t], %[t], 0xffff\n"
            ".set pop\n"
            : [t] "=&r"(t) : [sl] "r"(sled) : "t0", "t1", "t7", "t8", "t9", "memory");
    }
    return t;
}

/* One call after FlushCache: the first fetch of the code cold. */
static uint32_t __attribute__((noinline, aligned(64))) one(uint32_t target) {
    uint32_t t;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        " lui $t9, 0x1f80\n lhu $t0, 0x1120($t9)\n nop\n"
        " jalr %[tg]\n nop\n"
        " lhu $t1, 0x1120($t9)\n nop\n"
        " subu %[t], $t1, $t0\n andi %[t], %[t], 0xffff\n"
        ".set pop\n"
        : [t] "=&r"(t) : [tg] "r"(target) : "t0", "t1", "t9", "ra", "memory");
    return t;
}

static void putCode() {
    write32(MADR0, 0);                 /* nop */
    write32(BCR0, 0x03E00008u);        /* jr $ra */
    write32(CHCR0, 0);                 /* nop; nothing starts */
}

static void print2(int num, int den) {
    const int neg = num < 0; if (neg) num = -num;
    printf("%s%d.%02d", neg ? "-" : "", num / den, (100 * (num % den)) / den);
}

struct Case { const char* name; uint32_t addr; int calls; };

int main() {
    printf("\ncpu/code-in-io-time\n");
    printf("64 calls of `jr $ra; nop` from cached code: in the DMA0 registers (BCR0 entry: 2 I/O fetches, MADR0 entry: 3), and in RAM.\n");
    printf("Cycles a call at the system clock, fastest..slowest of 8 phases x 4 passes. No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t r2 = (uint32_t)ramStub2 & 0x1FFFFFFFu, r3 = (uint32_t)ramStub3 & 0x1FFFFFFFu;
    const Case cases[] = {
        { "empty bracket (64 x 2 nops)", 0, 0 },
        { "RAM cached, jr; nop", 0x80000000u | r2, 1 },
        { "RAM cached, nop; jr; nop", 0x80000000u | r3, 1 },
        { "RAM KSEG1, jr; nop", 0xA0000000u | r2, 1 },
        { "RAM KSEG1, nop; jr; nop", 0xA0000000u | r3, 1 },
        { "I/O KSEG1 BCR0 (2 fetches)", 0xBF801084u, 1 },
        { "I/O KSEG1 MADR0 (3 fetches)", 0xBF801080u, 1 },
        { "I/O KUSEG BCR0 (2 fetches)", 0x1F801084u, 1 },
        { "I/O KUSEG MADR0 (3 fetches)", 0x1F801080u, 1 },
        { "I/O KSEG0 BCR0 (2 fetches)", 0x9F801084u, 1 },
        { "I/O KSEG0 MADR0 (3 fetches)", 0x9F801080u, 1 },
    };
    const int NC = sizeof(cases) / sizeof(cases[0]);
    static uint32_t lo[16], hi[16], cold[16];
    putCode();
    flushCache();
    for (int c = 0; c < NC; c++) { lo[c] = 0xFFFFFFFFu; hi[c] = 0; }
    for (int pass = 0; pass < 4; pass++)
        for (int c = 0; c < NC; c++)
            for (uint32_t ph = 0; ph < 8; ph++) {
                run64(cases[c].addr, ph, cases[c].calls);
                const uint32_t t = run64(cases[c].addr, ph, cases[c].calls);
                if (t < lo[c]) lo[c] = t;
                if (t > hi[c]) hi[c] = t;
            }
    for (int c = 1; c < NC; c++) { flushCache(); cold[c] = one(cases[c].addr); }
    const uint32_t m0 = read32(MADR0), b0 = read32(BCR0), c0 = read32(CHCR0);
    write32(BCR0, 0);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    for (int c = 0; c < NC; c++) {
        printf("  %-30s ", cases[c].name);
        print2((int)lo[c], 64); printf(" .. "); print2((int)hi[c], 64);
        printf("   (64 calls %u..%u", (unsigned)lo[c], (unsigned)hi[c]);
        if (c) printf("; one cold call %u", (unsigned)cold[c]);
        printf(")\n");
    }
    printf("\nper extra fetch (3-word entry less the 2-word one, fastest), cycles:\n");
    for (int c = 2; c < NC; c += 2) {
        printf("  %-30s ", cases[c].name);
        print2((int)lo[c] - (int)lo[c - 1], 64);
        printf("\n");
    }
    printf("\nread back after: MADR0 %08x BCR0 %08x CHCR0 %08x\n", (unsigned)m0, (unsigned)b0, (unsigned)c0);
    printf("\ncpu/code-in-io-time: Done.\n");
    for (;;) VSync(0);
}
