/*
 * cpu/cpu-loop-tick15 - a slow-bus read behind two pages' stores, entered with the write queue already full (2026-10-28 night)
 *
 * WHY. cpu-loop-tick12's SPU-FAR (sw A+4; sw A+16K; SPUSTAT; andi) reads 27.45 a turn on the SCPH-9001 and 5501 - the CPU's
 * own path (the read 22 behind the first store, the second store after it). The bus's work a turn is less: two stores 12
 * and the read's own bus cycles 14 (cpu-loop-tick13's SPU-S3F, three pages, 32.02 = 18 + 14). In v0.377.212's model a turn
 * that starts with an OLDER store still on the bus (the last turn's second, which the read went ahead of) settles in a second
 * steady state: the read goes ahead of both of this turn's stores, a cycle sooner, and the turn is the bus's 26 for ever.
 * Nothing in tick12 starts it there; the refresh cut hwtests/refresh-store measured does (experiments/_mkrefx212.js:
 * SPU-FAR 26.33-26.63 on this core, CD-FAR the same with the CD's 4 - 16.34 against the consoles' 17.09). Whether the
 * console has that second state decides which of the two rules is wrong. This starts each loop with the queue full.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; P<k> = k stores to other 1 KB blocks (A+1K, 2K, 3K, 4K) just before the loop's first turn, nothing between; every
 * loop ends `addiu; bnez; nop`. R = the read (lbu for the CD's status, lhu for SPUSTAT) of v0; andi v0.
 *   CD-FAR / -P2 / -P4     sw A+4; sw A+16K; R          tick12's CD-FAR, entered after 0 / 2 / 4 stores
 *   SPU-FAR / -P2 / -P4    the same with SPUSTAT
 *   SPU-1ST-P4             sw A+4; R after 4 stores      one store a turn: the bus never the bound (a control)
 *   SPU-FAR-N1 / -N1P4     SPU-FAR with a nop after the andi, entered after 0 / 4 stores (the CPU's path a cycle longer)
 * If the second state is the console's, the -P rows read the bus's 26 (16 for the CD) plus the refresh's share; if not, the
 * rows without P plus the stores' one-time cost (~0.02).
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick15 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define CDSTAT   0x1F801800u
#define SPUSTAT  0x1F801DAEu

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));

#define N 1000
#define LOOP(name, pre, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n" pre "1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define P2 "  sw $zero, 0x400(%2)\n  sw $zero, 0x800(%2)\n"
#define P4 "  sw $zero, 0x400(%2)\n  sw $zero, 0x800(%2)\n  sw $zero, 0xC00(%2)\n  sw $zero, 0x1000(%2)\n"
#define FAR "  sw $v0, 4(%2)\n  sw $v0, 0x4000(%2)\n"
#define CDR "  lbu $v0, 0(%1)\n  andi $v0, $v0, 0xff\n"
#define SPR "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n"
LOOP(cdFar, "", FAR CDR)
LOOP(cdFarP2, P2, FAR CDR)
LOOP(cdFarP4, P4, FAR CDR)
LOOP(spFar, "", FAR SPR)
LOOP(spFarP2, P2, FAR SPR)
LOOP(spFarP4, P4, FAR SPR)
LOOP(sp1StP4, P4, "  sw $v0, 4(%2)\n" SPR)
LOOP(spFarN1, "", FAR SPR "  nop\n")
LOOP(spFarN1P4, P4, FAR SPR "  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick15\n");
    printf("A CD / SPU read behind two pages' stores, entered with the write queue full. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; };
    static const V vs[] = {
        { "CD-FAR", cdFar, CDSTAT }, { "CD-FAR-P2", cdFarP2, CDSTAT }, { "CD-FAR-P4", cdFarP4, CDSTAT },
        { "SPU-FAR", spFar, SPUSTAT }, { "SPU-FAR-P2", spFarP2, SPUSTAT }, { "SPU-FAR-P4", spFarP4, SPUSTAT },
        { "SPU-1ST-P4", sp1StP4, SPUSTAT }, { "SPU-FAR-N1", spFarN1, SPUSTAT }, { "SPU-N1P4", spFarN1P4, SPUSTAT },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick15: Done.\n");
    for (;;) VSync(0);
}
