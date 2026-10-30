/*
 * timers/timer-sync - the timer corners nobody measured: the target flag,
 * sync mode 3's bit 0, the /8 prescaler on a write, Timer0's hblank gate
 * against GP1(06h) and PAL, and Timer1's vblank sync modes.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, Timer rows T1, T3, T6, T7, T12, T13 and
 * T15). ps1-tests' timers log measured Timer0's four sync modes in NTSC at
 * the library's standard display range, and nocash's DIAG a few more rows;
 * these rules rest on nothing:
 *   T3   without reset-at-target, the target flag (MODE bit 11) sets when the
 *        counter passes the target here, not when it equals it (uncited).
 *   T1   sync mode 3 keeps MODE bit 0 as written after it switches to free run
 *        (DIAG's write test grades it OK, weakly; DuckStation and MiSTer clear
 *        it, Redux and mednafen keep the switch internal).
 *   T15  a write to the counter or MODE restarts the /8 prescaler (uncited).
 *   T6   Timer0's hblank gate is fixed (ticks below 608 or from 3168), not
 *        following GP1(06h)'s display range: ps1-tests' log was taken at the
 *        standard range only.
 *   T7   PAL's gate is 628/3188: DuckStation's overscan pair, unmeasured.
 *   T12  Timer1 sync mode 1 resets when vblank ENDS (M236, by analogy with
 *        Timer0's measured mode 2; mednafen and DuckStation agree).
 *   T13  Timer1 sync mode 2 resets at both edges (analogy).
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock as the clock
 * except in PRESCALER, which uses it.
 *   TARGET: Timer1 counting hblanks, target 20: the counter value read just
 *     before MODE first shows bit 11, without and with reset-at-target, and
 *     the values the counter passes through around the target.
 *   MODE 3: Timer0 set to sync mode 3 (0007h): MODE read at once and after 3
 *     lines; the counter at both.
 *   PRESCALER: Timer2 at sysclock/8; 0 written to the counter (then to MODE),
 *     K nops, the counter read, for K = 0..12, 32 times at pseudo-random
 *     phases: how many of the 32 read 1 or more at each K. A write that
 *     restarts the prescaler gives a sharp step; one that does not, a ramp.
 *   GATE: Timer0 at the system clock in sync mode 0 (pause during hblank) over
 *     16 lines (lines counted by Timer1): its count a line, at the standard
 *     display range (GP1(06h) 260h..C60h), a narrow one (400h..A00h), and both
 *     again in PAL mode (GP1(08h) bit 3; an NTSC TV may lose the picture for a
 *     moment - the TTY is unaffected). The mode is put back after.
 *   VBLANK: Timer1 counting hblanks in each sync mode (0-3), set just after a
 *     vblank starts: its count when the next vblank starts (I_STAT bit 0), then
 *     once a line for 40 lines, so where it pauses, resets and resumes shows
 *     against the vblank's own length.
 *
 * READ IT LIKE THIS. Every row beside this emulator's in the header.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js timer-sync`, seconds):
 *   TARGET    no reset: bit 11 first seen after the counter read 21 (past the
 *             target); with reset: the counter runs 0..19 and never reads 20,
 *             bit 11 seen after 19.
 *   MODE 3    MODE reads 0007h at once and 3 lines later (bit 0 kept); the
 *             counter 20 at once (the write landed in an hblank, which this
 *             core counts in), 4,487 after.
 *   PRESCALER 0 of 32 for K 0-3 and 32 from K 4 after a counter write; from K 5
 *             after a MODE write: a sharp step, the prescaler restarted.
 *   GATE      1,629.62-1,629.68 a line in every row: the gate is 2,560 GPU
 *             ticks whatever GP1(06h) says, NTSC (608..3168) and PAL (628..3188)
 *             alike.
 *   VBLANK    the vblank lasts 23-24 lines at the display range the BIOS left.
 *             Mode 0: 240 at vblank, held 24 samples, then counting on. Mode 1:
 *             counting through vblank to 263, then 1, 2, ... (the reset at
 *             vblank's end). Mode 2: 1..23 inside vblank, 0 after. Mode 3: free
 *             after the first vblank (262 at the second). MODE bit 0 reads 1 in
 *             all four. (A value shown twice is the 2,152-cycle sampling
 *             drifting against the 2,152.55-cycle line.)
 *
 * THE CONSOLES (SCPH-5501, 7501, 9001, two runs each, console day 2026-10-05/06), which answer five of the seven:
 *   TARGET    no reset: bit 11 seen at the FIRST read (0, once 1) - set before the section, kept through the MODE
 *             write (T_MODE_KEEP; T3 unanswerable here); with reset: 19 then 0, the 20 caught on the 9001 alone.
 *   MODE 3    MODE 0007h at once and 3 lines later (bit 0 kept: T1 answered); the counter 0 at once on all six -
 *             Unirom's display range, not the BIOS's 200h..C00h that this sideload keeps.
 *   PRESCALER 3-4 3-5 9-12 13-14 27-30 23-27 28-29 32 ... after a counter write, a RAMP: the /8 prescaler is NOT
 *             restarted by a write (T15, T2_PRESCALER).
 *   GATE      1,613.5-1,614.0 / 967.5-968.0: the gate FOLLOWS GP1(06h) (T6, T0_GATE), in PAL too (T7 moot).
 *   VBLANK    mode 0 239, 239 240x25 241; mode 1 237 ... 261 0x2 1; mode 2 0, 0x2 1 ... 23 0x15; mode 3 238 239 ...
 *             (T12, T13 and mode 3 by T1_VBLANK: Timer1's v-blank a line later and a line longer than the IRQ's).
 * ON THIS CORE AT v0.377.23: TARGET 0 / 0 (19 then 0), the consoles'; MODE 3 0007h, 0 at once; PRESCALER 17 16 24 25
 *   32 ... and 14 11 14 20 30 32 - a ramp, ~3 cycles later than the consoles'; GATE 1,614.81-1,614.87 / 968.87-968.93,
 *   one cycle a line over; VBLANK all four rows the consoles' exactly.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T0_TGT   0x1F801108u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T1_TGT   0x1F801118u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static void clock2(uint16_t mode) { write16(T2_MODE, mode); lastT = read16(T2_COUNT); }

/* The next line boundary: Timer1, counting hblanks, changes. */
static void nextLine() { const uint16_t c = read16(T1_COUNT); while (read16(T1_COUNT) == c) now(); }

/* PRESCALER: 0 written (to the counter, or to MODE with `mode`), K nops, the counter read. */
template <int K> __attribute__((noinline)) uint32_t pre(uint32_t mode, uint32_t which) {
    uint32_t v;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "lui $t9, 0x1f80\n\t"
        "bnez %[which], 1f\n\t"
        "nop\n\t"
        "sh $zero, 0x1120($t9)\n\t"
        "b 2f\n\t"
        "nop\n"
        "1:\n\t"
        "sh %[mode], 0x1124($t9)\n\t"
        "nop\n"
        "2:\n\t"
        ".rept %[k]\n\tnop\n\t.endr\n\t"
        "lhu %[v], 0x1120($t9)\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : [v] "=&r"(v) : [mode] "r"(mode), [which] "r"(which), [k] "i"(K) : "t9", "memory");
    return v;
}
typedef uint32_t (*preFn)(uint32_t, uint32_t);
static const preFn PRE[13] = { pre<0>, pre<1>, pre<2>, pre<3>, pre<4>, pre<5>, pre<6>, pre<7>, pre<8>, pre<9>, pre<10>, pre<11>, pre<12> };

static uint32_t rng = 0x2468ACE1u;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* GATE: Timer0 (system clock, sync mode 0) over 16 lines; its count a line in hundredths. */
static uint32_t gateCount() {
    write16(T1_MODE, 0x0100);                                   /* Timer1: hblanks, free run */
    write16(T0_MODE, 0x0001);                                   /* Timer0: system clock, pause during hblank */
    nextLine();
    const uint16_t a = read16(T0_COUNT);
    uint32_t sum = 0; uint16_t prev = a;
    for (int i = 0; i < 16; i++) { nextLine(); const uint16_t c = read16(T0_COUNT); sum += (uint16_t)(c - prev); prev = c; }
    write16(T0_MODE, 0x0000);
    return sum * 100 / 16;
}

int main() {
    printf("\ntimers/timer-sync\n");
    printf("Timer corners: the target flag, sync mode 3's bit 0, the /8 prescaler, the hblank gate, vblank sync. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    clock2(0x0000);
    clk = 0;

    /* TARGET */
    printf("TARGET (Timer1 counting hblanks, target 20):\n");
    for (int reset = 0; reset < 2; reset++) {
        write16(T1_TGT, 20);
        write16(T1_MODE, (uint16_t)(0x0100 | (reset ? 0x0008 : 0)));
        uint16_t seen[48]; int ns = 0; uint16_t last = 0xFFFF;
        int flagAt = -1;
        const uint32_t t0 = now();
        while (now() - t0 < 2152u * 40) {
            const uint16_t c = read16(T1_COUNT);
            if (c != last && ns < 48) { seen[ns++] = c; last = c; }
            const uint16_t m = read16(T1_MODE);                 /* a read clears bits 11-12 */
            if ((m & 0x0800) && flagAt < 0) { flagAt = c; }
        }
        printf("  %s: bit 11 first seen after the counter read %d; values:", reset ? "reset at target" : "no reset       ", flagAt);
        for (int i = 0; i < ns; i++) if ((seen[i] >= 15 && seen[i] <= 24) || (reset && seen[i] <= 3)) printf(" %u", seen[i]);
        printf("\n");
    }

    /* MODE 3 */
    write16(T0_MODE, 0x0007);                                   /* sync enable, mode 3, system clock */
    const uint16_t mA = read16(T0_MODE), cA = read16(T0_COUNT);
    write16(T1_MODE, 0x0100);
    nextLine(); nextLine(); nextLine();
    const uint16_t mB = read16(T0_MODE), cB = read16(T0_COUNT);
    write16(T0_MODE, 0x0000);
    printf("\nMODE 3 (Timer0 MODE 0007h): at once MODE %04x counter %u; 3 lines later MODE %04x counter %u\n",
           mA & 0x03FF, cA, mB & 0x03FF, cB);

    /* PRESCALER */
    printf("\nPRESCALER (Timer2 at sysclock/8; how many of 32 read 1 or more, K = 0..12 nops after the write):\n");
    for (int which = 0; which < 2; which++) {
        printf("  %s:", which ? "MODE written   " : "counter written");
        for (int k = 0; k <= 12; k++) {
            int hits = 0;
            clock2(0x0200); PRE[k](0x0200, (uint32_t)which);    /* warm the I-cache: a cold first trial reads late */
            for (int r = 0; r < 32; r++) {
                clock2(0x0200);
                for (uint32_t d = rnd() & 15; d; d--) __asm__ volatile("nop");
                if (PRE[k](0x0200, (uint32_t)which) >= 1) hits++;
            }
            printf(" %2d", hits);
        }
        printf("\n");
    }
    clock2(0x0000);

    /* GATE */
    const uint32_t st = read32(GP1);
    const uint32_t gp08 = ((st >> 17) & 3) | (((st >> 19) & 1) << 2) | (((st >> 20) & 1) << 3) | (((st >> 21) & 1) << 4) |
                          (((st >> 22) & 1) << 5) | (((st >> 16) & 1) << 6) | (((st >> 14) & 1) << 7);
    static uint32_t g[2][2];
    for (int pal = 0; pal < 2; pal++) {
        write32(GP1, 0x08000000u | (gp08 & ~8u) | (pal ? 8u : 0u));
        idle(33868800u / 10);
        for (int w = 0; w < 2; w++) {
            write32(GP1, 0x06000000u | (w ? ((0xA00u << 12) | 0x400u) : ((0xC60u << 12) | 0x260u)));
            idle(33868800u / 30);
            g[pal][w] = gateCount();
        }
    }
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write32(GP1, 0x08000000u | gp08);
    printf("\nGATE (Timer0, system clock, paused in hblank: counts a line, over 16 lines):\n");
    for (int pal = 0; pal < 2; pal++)
        printf("  %s  display 260h..C60h: %u.%02u   400h..A00h: %u.%02u\n", pal ? "PAL " : "NTSC",
               (unsigned)(g[pal][0] / 100), (unsigned)(g[pal][0] % 100), (unsigned)(g[pal][1] / 100), (unsigned)(g[pal][1] % 100));

    /* VBLANK */
    printf("\nVBLANK (Timer1 counting hblanks, set just after a vblank starts; the count when the next frame's vblank\n");
    printf("starts (I_STAT bit 0), then once a line (every 2,152 cycles) for 40 lines, runs as value x lines):\n");
    static const char* const vn[4] = { "0 pause in vblank", "1 reset at vblank", "2 reset, count in vblank", "3 wait once, then free" };
    for (int m = 0; m < 4; m++) {
        write32(I_STAT, ~1u);
        while (!(read32(I_STAT) & 1)) now();                    /* a vblank start */
        write32(I_STAT, ~1u);
        write16(T1_MODE, (uint16_t)(0x0100 | (m << 1) | 1));
        uint16_t prev = read16(T1_COUNT);
        while (!(read32(I_STAT) & 1)) { prev = read16(T1_COUNT); now(); }
        write32(I_STAT, ~1u);
        uint16_t v[40];
        const uint32_t t0 = now();
        for (int i = 0; i < 40; i++) { while (now() - t0 < 2152u * (uint32_t)i) {} v[i] = read16(T1_COUNT); }
        printf("  mode %-24s at vblank %4u, then", vn[m], prev);
        for (int i = 0; i < 40;) { int j = i; while (j < 40 && v[j] == v[i]) j++; if (j - i > 1) printf(" %ux%d", v[i], j - i); else printf(" %u", v[i]); i = j; }
        printf("   MODE bit 0 %u\n", read16(T1_MODE) & 1);
    }
    write16(T1_MODE, 0x0000);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimers/timer-sync: Done.\n");
    for (;;) VSync(0);
}
