/*
 * cpu/sr-mask - which bits of COP0 SR (r12) hold a written value?
 *
 * WHY (2026-10-30). Four answers, and nothing in this project has measured any of them:
 *   psx-spx dev (commit dd13185, branch cop0-sr-measured, its SR table; no console named): bits 6, 7, 20,
 *     21 and 23-27 read 0 after writing 1, every other bit holds, and no bit reads 1 after writing 0. A write mask of F04FFF3Fh.
 *   LSI's L64360 manual, chapter 14 (the same CPU core family, the closest manual psx-spx names): bits 27-23, 19, 17 and 7-6
 *     are reserved and read zero, TS is read-only, PE clears on writing 1. F045FF3Fh: bits 17 and 19 read 0 here, and hold there.
 *   DuckStation (cpu_types.h SR::WRITE_MASK): F27FFF3Fh: 20, 21 and 25 hold.
 *   This core (mtc0 in cop0op): every bit holds.
 *
 * WHAT IT DOES. Interrupts off, I_MASK 0. For each bit: SR written with that bit set, four nops, SR read, the entry value
 * written back; the same with the bit clear; SR read once more to prove the restore. Between the write and the restore the
 * code is registers only (srProbe, one asm block: no load, no store, no branch, no GTE). A per-bit table, the same bits all
 * at once, then the mask it adds up to against the four answers.
 *
 * LEFT OUT, AND WHY:
 *   bit 0 (IEc)   stays 0: the rule of this test is interrupts off.
 *   bit 1 (KUc)   stays 0: set, the next fetch from KSEG0 or KSEG1 is an address error.
 * FLIPPED, AND WHY IT IS SAFE IN THIS SEQUENCE:
 *   bits 2-5 (IEp, KUp, IEo, KUo)  a stack only rfe and exceptions move, and the window has neither.
 *   bits 8-15 (Sw, Im)             masks, and IEc is 0.
 *   bits 28-31 (CU0-3)             kernel mode reaches COP0 whatever CU0 says (L64360), and no coprocessor opcode runs.
 * ONLY FROM UNCACHED CODE (KSEG1), LAST, SO EVERYTHING ELSE HAS RUN FIRST:
 *   bit 22 (BEV)  moves the exception vectors, which matters only if an exception comes. None can: IEc is 0, I_MASK is 0, and
 *                 the window has no load, store, overflow, syscall or break. A CPU runs uncached with BEV set from reset on
 *                 (L64360: reset value 00400000h).
 *   bit 16 (IsC)  sends loads and stores to the cache. The window has none, and every fetch in it goes through KSEG1. The kernel
 *                 does the same at every FlushCache (scph1001 BFC0194Ch: SR = 10000h from uncached code; BFC01A38h: a further
 *                 `mtc0` with it set).
 *   bit 17 (SwC)  no effect by psx-spx, reserved by LSI; on a stock R3000 it swaps the caches for cached accesses, and the
 *                 window makes none. No BIOS here writes it (ten images scanned), so this bit's safety is that argument alone.
 * SR is checked first: with IEc or KUc set at that point (it cannot be, after EnterCriticalSection) nothing is flipped.
 *
 * READ IT LIKE THIS. holds = reads 1 after a 1 and 0 after a 0. reads 0 / reads 1 = a read-only bit. "others" on a row means a
 * second bit moved. The MASK lines leave bits 0-1 out of all four answers.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE (hwtests/README.md): run here first. ON THIS CORE AT v0.377.223 (`PSX_MODEL=SCPH-7501 node hwtests/run.js sr-mask
 * --raw`): SR 00000000 at entry and inside (under the runner on the virtual console 0000FF01, then 0000FB00); every bit holds,
 * mask FFFFFFFCh, no others, every restore right.
 * TESTED ON A SCRATCH CORE WITH F04FFF3Fh IN ITS MTC0: the nine bits read 0 and the rest hold, mask F04FFF3Ch, differs in 0
 * from psx-spx, 000A0000h from LSI, 02300000h from DuckStation. So the test tells a held bit from a dropped one.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_MASK 0x1F801074u

#define PSXSPX_MASK 0xF04FFF3Fu
#define LSI_MASK    0xF045FF3Fu
#define DUCK_MASK   0xF27FFF3Fu

/* Flipped in cached code; IsC, SwC and BEV apart, from KSEG1, last. */
#define UNCACHED_BITS ((1u << 16) | (1u << 17) | (1u << 22))
#define LEFT_OUT      ((1u << 0) | (1u << 1))

typedef uint32_t (*probe_fn)(uint32_t, uint32_t);
#define KSEG1(p) ((probe_fn)(((uint32_t)(p)) | 0xA0000000u))

static const char* const NAMES[32] = {
    "IEc", "KUc", "IEp", "KUp", "IEo", "KUo", "-", "-", "Sw0", "Sw1", "Im0", "Im1", "Im2", "Im3", "Im4", "Im5",
    "IsC", "SwC", "PZ", "CM", "PE", "TS", "BEV", "-", "-", "RE", "-", "-", "CU0", "CU1", "CU2", "CU3",
};

/* The whole window. One asm block, so the compiler can put nothing inside it. Four nops: a late write must not read as a
 * bit that does not hold (nocash: cop0 reads straight after a write, cop0r12.bit30's effect ~2 cycles late). */
extern "C" uint32_t __attribute__((noinline)) srProbe(uint32_t flipped, uint32_t orig) {
    uint32_t out;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "mtc0 %1, $12\n\t"
        "nop\n\tnop\n\tnop\n\tnop\n\t"
        "mfc0 %0, $12\n\t"
        "nop\n\t"
        "mtc0 %2, $12\n\t"
        "nop\n\tnop\n\tnop\n\tnop\n\t"
        ".set pop\n\t"
        : "=&r"(out) : "r"(flipped), "r"(orig));
    return out;
}

static inline uint32_t readSR() {
    uint32_t v;
    __asm__ volatile(".set push\n\t.set noreorder\n\tmfc0 %0, $12\n\tnop\n\t.set pop\n\t" : "=r"(v));
    return v;
}

static uint32_t held = 0, tested = 0;
static int restoreBad = 0;

static void row(probe_fn f, uint32_t sr0, int b) {
    const uint32_t bit = 1u << b;
    const uint32_t w1 = f(sr0 | bit, sr0);
    const uint32_t after1 = readSR();
    const uint32_t w0 = f(sr0 & ~bit, sr0);
    const uint32_t after0 = readSR();
    const int r1 = (w1 >> b) & 1, r0 = (w0 >> b) & 1;
    const uint32_t o1 = (w1 ^ sr0) & ~bit, o0 = (w0 ^ sr0) & ~bit;   /* any other bit moved */
    tested |= bit;
    if (r1 && !r0) held |= bit;
    if (after1 != sr0 || after0 != sr0) restoreBad = 1;
    printf("%2d %-3s entry %d  wrote 1 read %08x  wrote 0 read %08x  %s", b, NAMES[b], (sr0 >> b) & 1, w1, w0,
           r1 && !r0 ? "holds" : !r1 && !r0 ? "reads 0" : r1 && r0 ? "reads 1" : "INVERTS");
    if (o1 || o0) printf("  others %08x %08x", o1, o0);
    if (after1 != sr0 || after0 != sr0) printf("  RESTORE READ %08x %08x", after1, after0);
    printf("\n");
}

int main() {
    printf("\ncpu/sr-mask\n");
    const uint32_t srEntry = readSR();
    printf("SR at entry %08x\n", srEntry);

    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint32_t sr0 = readSR();
    printf("SR with interrupts off %08x, I_MASK was %04x\n", sr0, oldMask & 0xFFFF);

    if (sr0 & 3) {
        printf("IEc or KUc is set: nothing flipped\n");
    } else {
        /* the bits cached code may flip */
        printf("\nCACHED (bit, name, its value at entry, what the two writes read back, the verdict)\n");
        for (int b = 31; b >= 0; b--)
            if (!((LEFT_OUT | UNCACHED_BITS) & (1u << b))) row(srProbe, sr0, b);
        const uint32_t all = ~(LEFT_OUT | UNCACHED_BITS);
        printf("ALL cached bits  wrote %08x read %08x  wrote %08x read %08x\n", sr0 | all, srProbe(sr0 | all, sr0),
               sr0 & ~all, srProbe(sr0 & ~all, sr0));

        printf("\nUNCACHED, from KSEG1 (IsC, SwC, BEV: the header says why only here)\n");
        const probe_fn u = KSEG1(srProbe);
        row(u, sr0, 22);
        /* Bit 17 (SwC) is not flipped: no BIOS sets it, so nothing but an argument says it is safe, and this runs with nobody at the console. */
        row(u, sr0, 16);
        const uint32_t both = sr0 | (~LEFT_OUT & ~(1u << 17));
        printf("ALL but IEc, KUc wrote %08x read %08x\n", both, u(both, sr0));

        printf("\nMASK over the bits tested (%08x)\n", tested);
        printf("this console  %08x\n", held);
        printf("psx-spx dev   %08x  differs in %08x\n", PSXSPX_MASK & tested, (PSXSPX_MASK & tested) ^ held);
        printf("LSI L64360    %08x  differs in %08x\n", LSI_MASK & tested, (LSI_MASK & tested) ^ held);
        printf("DuckStation   %08x  differs in %08x\n", DUCK_MASK & tested, (DUCK_MASK & tested) ^ held);
        printf("restore %s, SR now %08x\n", restoreBad ? "FAILED ON SOME ROW" : "read back right on every row", readSR());
    }

    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
