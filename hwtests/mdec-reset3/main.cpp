/*
 * mdec/mdec-reset3 - mdec-reset2 with a spread, in one run: what each row reads over many tries (2026-10-31, for the
 * SCPH-5501).
 *
 * WHY. mdec-reset2's first run on the 5501: a store of the reset, N nops, and an enable or a command is lost for N 0-5 and
 * kept from N 6; a read inside that reads the bus, the value just written (40000000h behind a 40000000h, 40000001h behind
 * the command). But its plain `read` column went 80040000 80040000 junk junk junk 80040000 80040000 junk, and one run of a
 * row is not a measurement (CLAUDE.md, rule 3). The junk climbs with the clock (6BD1h, 9C76h, CD16h ... 160B1h), so it looks
 * like clk itself, the last thing stored in RAM. A test that parks needs a RESET for each run, so this one takes its own
 * spread, and it asks the question the junk raises: is a read inside the window the bus, and what put that on it?
 *
 * WHAT IT DOES. 24 tries of each case at each N from 0 to 15, cache warm, and for each the values seen with how many
 * times. Before each try it puts a known value on the bus, a store of 5A5A5A5Ah to a scratchpad-free RAM word, so junk
 * that reads 5A5A5A5Ah is that store and not the clock. Then the same with a read of the status just before the reset,
 * and with a Timer 2 read between the reset and the access, which is what mdec-reset's N 1 row has and N 0 does not.
 *
 * READ IT LIKE THIS. A row with one value is a fact; one with several is a phase or the bus, and the counts say how far.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static volatile uint32_t marker;

#define SLED ".rept %[n]\n nop\n .endr\n"
#define TOP ".set push\n.set noreorder\n lui $t0, 0x1F80\n lui $t1, 0x8000\n lui $t2, 0x6000\n lui $t3, 0x4000\n ori $t3, $t3, 1\n"
#define MARK "lui $t4, 0x5A5A\n ori $t4, $t4, 0x5A5A\n sw $t4, 0(%[m])\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
#define END ".set pop\n"
#define CLOBBER "t0", "t1", "t2", "t3", "t4", "memory"

template <int N> __attribute__((noinline)) static uint32_t rd() {
    uint32_t v;
    __asm__ volatile(TOP MARK "sw $t1, 0x1824($t0)\n" SLED "lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v) : [n] "i"(N), [m] "r"(&marker) : CLOBBER);
    return v;
}
template <int N> __attribute__((noinline)) static uint32_t wrRd() {
    uint32_t v;
    __asm__ volatile(TOP MARK "sw $t1, 0x1824($t0)\n" SLED "sw $t2, 0x1824($t0)\n lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v) : [n] "i"(N), [m] "r"(&marker) : CLOBBER);
    return v;
}
template <int N> __attribute__((noinline)) static uint32_t rdFirst() {
    uint32_t v, w;
    __asm__ volatile(TOP "lw %[w], 0x1824($t0)\n nop\n sw $t1, 0x1824($t0)\n" SLED "lw %[v], 0x1824($t0)\n nop\n" END : [v] "=&r"(v), [w] "=&r"(w) : [n] "i"(N) : CLOBBER);
    return v;
}
template <int N> __attribute__((noinline)) static uint32_t en() {
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n" SLED "sw $t2, 0x1824($t0)\n" END : : [n] "i"(N) : CLOBBER);
    return 0;
}
template <int N> __attribute__((noinline)) static uint32_t tmrEn() {
    /* A Timer 2 read between: any other register on the bus, as mdec-reset's N 1 has. */
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n lhu $t4, 0x1120($t0)\n" SLED "sw $t2, 0x1824($t0)\n" END : : [n] "i"(N) : CLOBBER);
    return 0;
}
template <int N> __attribute__((noinline)) static uint32_t ramEn() {
    /* A RAM store between: the queue busy with something that is not the MDEC. */
    __asm__ volatile(TOP "sw $t1, 0x1824($t0)\n sw $t1, 0(%[m])\n" SLED "sw $t2, 0x1824($t0)\n" END : : [n] "i"(N), [m] "r"(&marker) : CLOBBER);
    return 0;
}

typedef uint32_t (*Case)();
#define ALL(f) { f<0>, f<1>, f<2>, f<3>, f<4>, f<5>, f<6>, f<7>, f<8>, f<9>, f<10>, f<11>, f<12>, f<13>, f<14>, f<15> }
static const Case RD[16] = ALL(rd), WRRD[16] = ALL(wrRd), RDF[16] = ALL(rdFirst), EN[16] = ALL(en), TMR[16] = ALL(tmrEn), RAM[16] = ALL(ramEn);

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }

struct Seen { uint32_t v[4]; uint8_t c[4]; uint8_t others; };
static void add(Seen& s, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        if (s.c[i] && s.v[i] == v) { s.c[i]++; return; }
        if (!s.c[i]) { s.v[i] = v; s.c[i] = 1; return; }
    }
    s.others++;
}
static const int TRIES = 24;
static void readRow(Case c, Seen& s) {
    for (int i = 0; i < 2 + TRIES; i++) { settle(); const uint32_t v = c(); if (i >= 2) add(s, v); }
}
/* An enable's fate: a table command 400 cycles on, and bit 28 says whether it was kept. */
static int keptOf(Case c) {
    int kept = 0;
    for (int i = 0; i < 2 + TRIES; i++) {
        settle(); c(); waitCycles(400);
        write32(MDEC0, 0x40000001u);
        const uint32_t s = read32(MDEC1);
        if (i >= 2 && (s & (1u << 28))) kept++;
    }
    return kept;
}
static void printRow(const char* name, int n, const Seen& s) {
    printf("%s N %2d:", name, n);
    for (int i = 0; i < 4 && s.c[i]; i++) printf(" %08xx%d", (unsigned)s.v[i], (int)s.c[i]);
    if (s.others) printf(" +%d more", (int)s.others);
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset3\n");
    printf("The reset's window over %d tries each, cache warm, N nops behind the reset. No assertions.\n\n", TRIES);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    static Seen rd[16], wr[16], rf[16];
    static int kept[3][16];
    for (int n = 0; n < 16; n++) {
        readRow(RD[n], rd[n]);
        readRow(WRRD[n], wr[n]);
        readRow(RDF[n], rf[n]);
        kept[0][n] = keptOf(EN[n]);
        kept[1][n] = keptOf(TMR[n]);
        kept[2][n] = keptOf(RAM[n]);
    }
    settle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int n = 0; n < 16; n++) printRow("read      ", n, rd[n]);
    for (int n = 0; n < 16; n++) printRow("read(rd1st)", n, rf[n]);
    for (int n = 0; n < 16; n++) printRow("wr+rd     ", n, wr[n]);
    printf("\nEnables kept, of %d: N, plain, with a Timer 2 read between, with a RAM store between\n", TRIES);
    for (int n = 0; n < 16; n++) printf("%2d  %2d  %2d  %2d\n", n, kept[0][n], kept[1][n], kept[2][n]);
    printf("\nmdec/mdec-reset3: Done.\n");
    for (;;) {}   /* park after Done. */
}
