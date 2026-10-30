/*
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
#define LOOP(name, body) static void name(volatile uint32_t* a, volatile uint32_t* s) { \
    uint32_t n = N; uint32_t t2 = T2_COUNT; \
    asm volatile(".set push\n.set noreorder\n" \
        "la $t0, 2f\n  sw $t0, 0(%1)\n  sw $t0, 0(%2)\n" \
        "1:\n" body "\n2:\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(a), "r"(s), "r"(t2) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define LD2 "  lw $v1, 0(%1)\n  lw $a1, 4(%1)\n"
LOOP(ctrl, LD2 "  nop\n")
LOOP(addu, LD2 "  subu $a2, $v1, $v1\n")
LOOP(jr, LD2 "  jr $v1\n  nop\n")
LOOP(beq, LD2 "  beq $v1, $zero, 2f\n  nop\n")
LOOP(swdata, LD2 "  sw $v1, 8(%1)\n")
LOOP(addr, LD2 "  lw $a2, 0($v1)\n")
LOOP(pos2, LD2 "  nop\n  subu $a2, $v1, $v1\n")
LOOP(same, "  lw $v1, 0(%1)\n  lw $v1, 4(%1)\n  subu $a2, $v1, $v1\n")
LOOP(spFirst, "  lw $v1, 0(%2)\n  lw $a1, 4(%1)\n  subu $a2, $v1, $v1\n")
LOOP(spSecond, "  lw $v1, 0(%1)\n  lw $a1, 4(%2)\n  subu $a2, $v1, $v1\n")
LOOP(ioFirst, "  lhu $v1, 0(%3)\n  lw $a1, 4(%1)\n  subu $a2, $v1, $v1\n")
LOOP(ioSecond, "  lw $v1, 0(%1)\n  lhu $a1, 0(%3)\n  subu $a2, $v1, $v1\n")
LOOP(three, LD2 "  lw $a2, 8(%1)\n  subu $a3, $v1, $v1\n")
LOOP(epilog, LD2 "  jr $v1\n  addiu $a2, $a2, 8\n")

static uint32_t timeIt(void (*f)(volatile uint32_t*, volatile uint32_t*)) {
    const uint16_t a = read16(T2_COUNT);
    f(ramW, SPAD);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick14\n");
    printf("The first of two loads late for its reader in the second's delay slot: by reader, load kind, position. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(volatile uint32_t*, volatile uint32_t*); };
    static const V vs[] = {
        { "CTRL", ctrl }, { "ADDU", addu }, { "JR", jr }, { "BEQ", beq }, { "SWDATA", swdata }, { "ADDR", addr },
        { "POS2", pos2 }, { "SAME", same }, { "SP-FIRST", spFirst }, { "SP-SECOND", spSecond }, { "IO-FIRST", ioFirst },
        { "IO-SECOND", ioSecond }, { "THREE", three }, { "EPILOG", epilog },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick14: Done.\n");
    for (;;) VSync(0);
}
