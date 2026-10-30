/*
 * cpu/fill-gap - does an instruction-cache fill cost more after the RAM bus has been idle?
 *
 * WHY (v0.377.26). hwtests/icache-tagmatch reads every timed call that misses the cache 4 cycles slower on the SCPH-5501,
 * 7501 and 9001 than here (COLD 29 against 25, W1 27 against 23), whatever the misses are: a jump to word 0, a jump to
 * word 1, a fall-through into a delay slot, one fill or two. hwtests/line-fill's warm call - its only fill the delay
 * slot of the call back to the timer - reads 82 there and 78 here, and its back-to-back fills cost exactly what the
 * consoles' do (9.25 a line). So the 4 cycles seem to belong to the first fill after a stretch of cached code, not to a
 * kind of miss. pcsx-redux's load-timings gap probe (PR #2091, silicon) found a RAM LOAD's cost the same after any idle
 * gap - "a closed-page controller". A fill that pays for an idle bus where a load does not would be an instruction-side
 * cost; a DRAM refresh left waiting while the bus is idle would show in both.
 *
 * WHAT IT DOES. Two 2-instruction functions (jr ra; nop) 4 KB apart, so they share one cache line. Each sample calls B
 * (A's line now B's), runs a cached delay loop of D iterations (3 cycles each, no RAM access), then times a call to A -
 * a fill at a jump target - between two Timer 2 reads; WARM is the same with A called first, not B. Then the control:
 * after the same delay, a RAM load (lw, its register read at once) between the two reads. D = 0..512; each row the
 * fastest and slowest of 8 samples. The difference COLD - WARM is the fill.
 *
 * READ IT LIKE THIS. A flat COLD - WARM says idle time does not matter to a fill. A step at some D says it does, and D
 * is the controller's idle threshold; a LOAD row that steps with it says it is the bus (a refresh waiting), one that
 * stays flat says it is the fill.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fill-gap`.
 * ON THIS CORE AT v0.377.26 (no idle cost): COLD 14, WARM 7, COLD - WARM 7 and LOAD 13 at every D (slowest +4..5 where a
 * DRAM refresh lands). icache-tagmatch's +4 would read here as COLD - WARM 11 past some D.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

uint32_t ramWord;

extern "C" void fgA(void);
extern "C" void fgB(void);
__asm__(
    ".text\n"
    ".balign 4096\n"
    ".global fgA\n"
    ".set push\n.set noreorder\n"
    "fgA:\n    jr $ra\n    nop\n"
    ".set pop\n"
    ".balign 4096\n"
    ".global fgB\n"
    ".set push\n.set noreorder\n"
    "fgB:\n    jr $ra\n    nop\n"
    ".set pop\n");

/* One sample: `pre` (fgA: warm, fgB: A's line evicted), d delay iterations, then the timed call or load. The whole
 * sequence is one asm block so nothing the compiler schedules lands between the two reads. */
static uint32_t __attribute__((noinline, aligned(64))) sampleCall(void (*pre)(void), uint32_t d) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n"
                     "jalr %1\n nop\n"
                     "beqz %2, 2f\n nop\n"
                     "1: addiu %2, %2, -1\n bnez %2, 1b\n nop\n"
                     "2: lui $t9, 0x1f80\n"
                     "lhu $t8, 0x1120($t9)\n"
                     "jal fgA\n nop\n"
                     "lhu $t7, 0x1120($t9)\n"
                     "nop\n"
                     "subu %0, $t7, $t8\n"
                     ".set pop\n"
                     : "=&r"(r), "+r"(pre), "+r"(d) : : "t7", "t8", "t9", "ra", "memory");
    return r & 0xFFFF;
}
static uint32_t __attribute__((noinline, aligned(64))) sampleLoad(uint32_t p, uint32_t d) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n"
                     "beqz %2, 2f\n nop\n"
                     "1: addiu %2, %2, -1\n bnez %2, 1b\n nop\n"
                     "2: lui $t9, 0x1f80\n"
                     "lhu $t8, 0x1120($t9)\n"
                     "lw $t6, 0(%1)\n"
                     "nop\n"
                     "addu $t6, $t6, $t6\n"
                     "lhu $t7, 0x1120($t9)\n"
                     "nop\n"
                     "subu %0, $t7, $t8\n"
                     ".set pop\n"
                     : "=&r"(r), "+r"(p), "+r"(d) : : "t6", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}

struct Span { uint32_t lo, hi; };
static Span span8(int kind, uint32_t d) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 9; k++) {
        const uint32_t t = kind == 0 ? sampleCall(fgB, d) : kind == 1 ? sampleCall(fgA, d) : sampleLoad((uint32_t)&ramWord, d);
        if (k == 0) continue;                       /* the first warms the harness */
        if (t < s.lo) s.lo = t;
        if (t > s.hi) s.hi = t;
    }
    return s;
}

int main() {
    printf("\ncpu/fill-gap\n");
    printf("A call that misses the instruction cache, and a RAM load, after D cached delay iterations (3 cycles each).\n");
    printf("Timer 2 at the system clock, fastest..slowest of 8. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static const uint32_t ds[] = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512 };
    printf("     D   COLD (a fill)   WARM        COLD-WARM   LOAD\n");
    for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
        const uint32_t d = ds[i];
        const Span c = span8(0, d), w = span8(1, d), l = span8(2, d);
        printf("  %4u   %3u..%-3u       %3u..%-3u    %4d        %3u..%-3u\n", (unsigned)d,
               (unsigned)c.lo, (unsigned)c.hi, (unsigned)w.lo, (unsigned)w.hi, (int)c.lo - (int)w.lo, (unsigned)l.lo, (unsigned)l.hi);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
