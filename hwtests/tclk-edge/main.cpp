/*
 * timer/tclk-edge - on a MODIFIED console, how many instructions after an
 * edge on Timer 0's clock pin does a read of the counter see it?
 *
 * WHY (ROAD A8 "timer-dump's 5 rows", dug 2026-09-27; ROAD B). ps1-tests'
 * timer-dump runs only on a console whose CPU pin 160 (TCLK0) is cut from
 * the GPU and wired to pin 70 (RTS), so the program clocks Timer 0 by hand
 * through SIO_CTRL bit 5; it reads the counter right after each write. Five
 * of its 283 rows differ here, and four are the FIRST counting edge after the
 * mode or counter writes: hardware reads 0 where this core reads 1, and the
 * next sample agrees. Sony's BBS (scea/OS.txt 1918-1921) says the pixel-clock
 * and hblank counters are "not arbitrated by hardware" and sometimes read
 * wrong - a race between the increment and the read. How late an edge
 * reaches the counter settles which.
 *
 * WHAT IT DOES. Interrupts off. Timer 0 on its external clock (mode 0100h,
 * the dot clock's source - the RTS wire on the modified console), counter set
 * to 0. For N = 0..8, from cached code (the second of two calls): RTS on, N
 * nops, read; RTS off, N nops, read; twice more - eight reads after eight
 * edges. Then the same with the counter resetting at its target (mode 0108h,
 * target 2), N = 0 and 8.
 *
 * READ IT LIKE THIS. Each row is the eight values read, one after each RTS
 * write. A counter that sees an edge within N instructions counts 1 2 3 4 or
 * 0 1 1 2 2 ... (one edge of each pair counting); the N where a row first
 * reads what the rows under it read is the latency. A stock console reads
 * nothing useful: TCLK0 is the GPU's dot clock there.
 * RUN IT ONLY ON THE MODIFIED CONSOLE timer-dump's header describes.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js tclk-edge --tclk0-rts`, the
 * wire modelled as cputest does for timer-dump alone):
 *   mode 0100h, every N = 0..8:  1 1 2 2 3 3 4 4
 *   mode 0108h, target 2, N = 0: 1 1 2 0 1 1 2 0;  N = 8: 1 1 0 0 1 1 0 0
 * So the RTS-on write counts, and the very next instruction's read sees it:
 * no latency at any N. timer-dump's console reads the first counting edge as
 * 0 where this reads 1 - a console whose N = 0 row starts 0 and whose later
 * rows start 1 has a latency; one whose rows all start 0 skips the first edge.
 * At the target the counter reads 2 at once and 0 by the next edge (N = 0),
 * or is already back at 0 within 8 nops (N = 8). Without --tclk0-rts (a
 * stock console) Timer 0 counts the dot clock and the rows mean nothing.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT  0x1F801100u
#define T0_MODE   0x1F801104u
#define T0_TARGET 0x1F801108u
#define SIO_CTRL  0x1F80105Au

typedef void (*Seq)(uint32_t, uint32_t, uint16_t *);

/* RTS on = SIO_CTRL bit 5 set to 0 as timer-dump writes it ("sh $zero" = gpio HIGH), off = 20h. */
#define EDGE(K) \
static void __attribute__((noinline, aligned(64))) edge##K(uint32_t t, uint32_t s, uint16_t *out) { \
    uint32_t a, b, c, d, e, f, g, h; \
    __asm__ volatile(".set noreorder\n li $t9, 0x20\n" \
        " sh $zero, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %0, 0(%9)\n" \
        " sh $t9, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %1, 0(%9)\n" \
        " sh $zero, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %2, 0(%9)\n" \
        " sh $t9, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %3, 0(%9)\n" \
        " sh $zero, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %4, 0(%9)\n" \
        " sh $t9, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %5, 0(%9)\n" \
        " sh $zero, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %6, 0(%9)\n" \
        " sh $t9, 0(%8)\n .rept " #K "\n nop\n .endr\n lhu %7, 0(%9)\n nop\n .set reorder\n" \
        : "=&r"(a), "=&r"(b), "=&r"(c), "=&r"(d), "=&r"(e), "=&r"(f), "=&r"(g), "=&r"(h) : "r"(s), "r"(t) : "t9", "memory"); \
    out[0] = a; out[1] = b; out[2] = c; out[3] = d; out[4] = e; out[5] = f; out[6] = g; out[7] = h; }
EDGE(0) EDGE(1) EDGE(2) EDGE(3) EDGE(4) EDGE(5) EDGE(6) EDGE(7) EDGE(8)

static const Seq edges[9] = { edge0, edge1, edge2, edge3, edge4, edge5, edge6, edge7, edge8 };
static uint16_t rows[9][8], targetRows[2][8];

/* The mode and counter set, RTS left off; then the sequence twice, the counter set again between (cached run). */
static void run(Seq f, uint16_t mode, uint16_t *out) {
    uint16_t scratch[8];
    write16(SIO_CTRL, 0x20);
    write16(T0_MODE, mode); write16(T0_COUNT, 0);
    f(T0_COUNT, SIO_CTRL, scratch);
    write16(SIO_CTRL, 0x20);
    write16(T0_MODE, mode); write16(T0_COUNT, 0);
    f(T0_COUNT, SIO_CTRL, out);
}

int main() {
    printf("\ntimer/tclk-edge\n");
    printf("Timer 0 clocked by hand through RTS (a MODIFIED console: TCLK0 wired to RTS). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), tg = read16(T0_TARGET);
    write16(SIO_CTRL, 1 << 6);                 /* SIO1 reset, as timer-dump's sioInit */
    for (int k = 0; k < 9; k++) run(edges[k], 0x0100, rows[k]);
    write16(T0_TARGET, 2);
    run(edges[0], 0x0108, targetRows[0]);
    run(edges[8], 0x0108, targetRows[1]);
    write16(T0_TARGET, tg); write16(T0_MODE, m0);
    write16(SIO_CTRL, 1 << 6);
    ExitCriticalSection();

    printf("mode 0100h, counter from 0: the value read after each RTS write (on, off, on, off, ...)\n");
    for (int k = 0; k < 9; k++) {
        printf("  N=%d:", k);
        for (int i = 0; i < 8; i++) printf(" %4u", rows[k][i]);
        printf("\n");
    }
    printf("mode 0108h, target 2:\n");
    for (int r = 0; r < 2; r++) {
        printf("  N=%d:", r ? 8 : 0);
        for (int i = 0; i < 8; i++) printf(" %4u", targetRows[r][i]);
        printf("\n");
    }
    printf("\ntimer/tclk-edge: Done.\n");
    for (;;) VSync(0);
}
