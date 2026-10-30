/*
 * cpu/cpu-loop-tick17 - v0.377.214's three OPEN uncached rows taken apart: a scratchpad load beside a RAM one, and a store two
 * on (2026-10-29 night).
 *
 * WHY. hwtests/cpu-loop-tick16 (tick14's rows through their KSEG1 alias) on the SCPH-5501 and 9001, alike to the hundredth:
 * v0.377.214 (UNC_LOAD_OVERLAP kept for a reader at i+2 behind a second RAM load, lost to a third load) fits 11 of its 14
 * uncached rows; SP-FIRST 40.87 / SP-SECOND 41.81 read 39.95 / 40.86 here (a cycle short) and SWDATA 52.08 reads 53.10 (a
 * cycle long). Each is taken apart here - with and without its reader, the reader a slot later, the load alone - cached and
 * uncached, as tick16 times them.
 *
 * WHAT IT DOES. As tick16: interrupts off; each loop 1,000 times between two Timer 2 reads, cold, hot and uncached (through
 * its KSEG1 alias), cycles an iteration. A = a RAM word, S = a scratchpad word (each holding the loop's end address).
 *   CTRL       lw v1, A; lw a1, A+4; nop                          tick16's base
 *   RAM-ALONE  lw v1, A; nop; nop                                 one RAM load (unc-load's L)
 *   SP-ALONE   lw v1, S; nop; nop                                 one scratchpad load
 *   SP-RD2     lw v1, S; nop; subu a2, v1, v1                     ... and its reader two on
 *   SP-SP      lw v1, S; lw a1, S+4; subu a2, v1, v1              both loads from the scratchpad
 *   SPF-NR     lw v1, S; lw a1, A+4; nop                          SP-FIRST without its reader
 *   SPF-RD3    lw v1, S; lw a1, A+4; nop; subu a2, v1, v1         SP-FIRST's reader a slot later
 *   SP-FIRST   lw v1, S; lw a1, A+4; subu a2, v1, v1              tick16's
 *   SPS-NR     lw v1, A; lw a1, S+4; nop                          SP-SECOND without its reader
 *   SPS-RD3    lw v1, A; lw a1, S+4; nop; subu a2, v1, v1         SP-SECOND's reader a slot later
 *   SP-SECOND  lw v1, A; lw a1, S+4; subu a2, v1, v1              tick16's
 *   SW1        lw v1, A; nop; sw v1, A+8                          the load's register stored two on, ONE load
 *   SW-OTHER   lw v1, A; lw a1, A+4; sw a2, A+8                   a store at i+2 of another register
 *   SW-SP      lw v1, A; lw a1, A+4; sw v1, S+8                   SWDATA's store to the scratchpad
 *   SW-RD3     lw v1, A; lw a1, A+4; nop; sw v1, A+8              SWDATA's store a slot later
 *   SWDATA     lw v1, A; lw a1, A+4; sw v1, A+8                   tick16's
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick17 --ntsc`. cpu-loop-tick16's header follows.
 *
 * cpu/cpu-loop-tick16 - cpu-loop-tick14's rows UNCACHED: each loop called through its KSEG1 alias (A0000000h + its address),
 * beside the same loop cached (experiments/_mkcpuloop16.js). v0.377.212's rule 5 - M209's overlapped second load given up for
 * a reader of the first's register - was measured cached; nocash DIAG's timing bracket and the BIOS ROM run it uncached.
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick16 --ntsc`. cpu-loop-tick14's header follows.
 *
 * cpu/cpu-loop-tick14 - when is the first of two back-to-back loads late for its reader in the second's delay slot: by the
 * reader (ALU, jump register, branch, store data, address), by where each load reads (RAM, scratchpad, Timer 2), by position
 * (2026-10-28 night)
 *
 * WHY (v0.377.211's KNOWN-OPEN beside loadOverlap). hwtests/cpu-loop-tick9/10 on the SCPH-9001 and 5501: `lhu v1, A; lw a1,
 * A+4; subu a2, v1, v1` costs a cycle a turn more than without the subu's read - after nothing, nops, a Timer 2 read, an
 * SPUSTAT read, stores - where this core charges nothing. Charging it everywhere (experiments/_mklov211.js) moves cputest's
 * access-time and chain-looping, io-pair2, load-fill3 and store-chain off the consoles: those programs' compiled C is full
 * of the same shape (`lw ra; lw s0; jr ra` returns from every function), so either the console does not charge every reader
 * alike, or not for every kind of load. This asks each.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word holding the address of the loop's own end label (so a jump through it lands where the loop goes on); S = a scratchpad
 * word holding the same; every loop ends `addiu; bnez; nop`.
 *   CTRL       lw v1, A; lw a1, A+4; nop                          no reader (the base)
 *   ADDU       lw v1, A; lw a1, A+4; subu a2, v1, v1               an ALU reader (tick10's LD-V)
 *   JR         lw v1, A; lw a1, A+4; jr v1; nop                    a jump-register reader (every function's return)
 *   BEQ        lw v1, A; lw a1, A+4; beq v1, zero, x; nop          a branch reader
 *   SWDATA     lw v1, A; lw a1, A+4; sw v1, A+8                    the first's register as a store's data
 *   ADDR       lw v1, A; lw a1, A+4; lw a2, 0(v1)                  ... as a load's address
 *   POS2       lw v1, A; lw a1, A+4; nop; subu a2, v1, v1          the reader one later
 *   SAME       lw v1, A; lw v1, A+4; subu a2, v1, v1               the same register (M209: no overlap)
 *   SP-FIRST   lw v1, S; lw a1, A+4; subu a2, v1, v1               the first from the scratchpad
 *   SP-SECOND  lw v1, A; lw a1, S+4; subu a2, v1, v1               the second from the scratchpad
 *   IO-FIRST   lhu v1, T2; lw a1, A+4; subu a2, v1, v1             the first an on-die read
 *   IO-SECOND  lw v1, A; lhu a1, T2; subu a2, v1, v1               the second an on-die read
 *   THREE      lw v1, A; lw a1, A+4; lw a2, A+8; subu a3, v1, v1   the first's reader in the third's delay slot
 *   EPILOG     lw v1, A; lw a1, A+4; jr v1; addiu a2, a2, 8        a return with its delay slot used
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick14 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramW[16] __attribute__((aligned(64)));
#define SPAD ((volatile uint32_t*)0x1F800000u)

#define N 1000
/* %1 = A (RAM), %2 = S (scratchpad), %3 = Timer 2; label 2: is the loop's end, its address stored at A and S. */
static uint32_t seg;   /* 0 cached, 20000000h through the KSEG1 alias: the loop's own end label in the segment it runs from */
#define LOOP(name, body) static void __attribute__((noinline)) name(volatile uint32_t* a, volatile uint32_t* s) { \
    uint32_t n = N; uint32_t t2 = T2_COUNT; uint32_t sg = seg; \
    asm volatile(".set push\n.set noreorder\n" \
        "la $t0, 2f\n  or $t0, $t0, %4\n  sw $t0, 0(%1)\n  sw $t0, 0(%2)\n" \
        "1:\n" body "\n2:\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(a), "r"(s), "r"(t2), "r"(sg) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define LD2 "  lw $v1, 0(%1)\n  lw $a1, 4(%1)\n"
LOOP(ctrl, LD2 "  nop\n")
LOOP(ramAlone, "  lw $v1, 0(%1)\n  nop\n  nop\n")
LOOP(spAlone, "  lw $v1, 0(%2)\n  nop\n  nop\n")
LOOP(spRd2, "  lw $v1, 0(%2)\n  nop\n  subu $a2, $v1, $v1\n")
LOOP(spSp, "  lw $v1, 0(%2)\n  lw $a1, 4(%2)\n  subu $a2, $v1, $v1\n")
LOOP(spfNr, "  lw $v1, 0(%2)\n  lw $a1, 4(%1)\n  nop\n")
LOOP(spfRd3, "  lw $v1, 0(%2)\n  lw $a1, 4(%1)\n  nop\n  subu $a2, $v1, $v1\n")
LOOP(spFirst, "  lw $v1, 0(%2)\n  lw $a1, 4(%1)\n  subu $a2, $v1, $v1\n")
LOOP(spsNr, "  lw $v1, 0(%1)\n  lw $a1, 4(%2)\n  nop\n")
LOOP(spsRd3, "  lw $v1, 0(%1)\n  lw $a1, 4(%2)\n  nop\n  subu $a2, $v1, $v1\n")
LOOP(spSecond, "  lw $v1, 0(%1)\n  lw $a1, 4(%2)\n  subu $a2, $v1, $v1\n")
LOOP(sw1, "  lw $v1, 0(%1)\n  nop\n  sw $v1, 8(%1)\n")
LOOP(swOther, LD2 "  sw $a2, 8(%1)\n")
LOOP(swSp, LD2 "  sw $v1, 8(%2)\n")
LOOP(swRd3, LD2 "  nop\n  sw $v1, 8(%1)\n")
LOOP(swdata, LD2 "  sw $v1, 8(%1)\n")

typedef void (*LoopFn)(volatile uint32_t*, volatile uint32_t*);
static uint32_t timeIt(LoopFn f0, bool uncached) {
    seg = uncached ? 0x20000000u : 0;
    const LoopFn f = uncached ? (LoopFn)((uint32_t)f0 | 0xA0000000u) : f0;
    const uint16_t a = read16(T2_COUNT);
    f(ramW, SPAD);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick17\n");
    printf("tick16's open uncached rows taken apart: scratchpad loads, a store two on. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(volatile uint32_t*, volatile uint32_t*); };
    static const V vs[] = {
        { "CTRL", ctrl }, { "RAM-ALONE", ramAlone }, { "SP-ALONE", spAlone }, { "SP-RD2", spRd2 }, { "SP-SP", spSp },
        { "SPF-NR", spfNr }, { "SPF-RD3", spfRd3 }, { "SP-FIRST", spFirst }, { "SPS-NR", spsNr }, { "SPS-RD3", spsRd3 },
        { "SP-SECOND", spSecond }, { "SW1", sw1 }, { "SW-OTHER", swOther }, { "SW-SP", swSp }, { "SW-RD3", swRd3 },
        { "SWDATA", swdata },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][3];
    for (int i = 0; i < NV; i++) { for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, false); c[i][2] = timeIt(vs[i].f, true); }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot     uncached   (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10), (unsigned)(c[i][2] / N), (unsigned)(c[i][2] % N / 10));
    printf("\ncpu/cpu-loop-tick17: Done.\n");
    for (;;) VSync(0);
}
