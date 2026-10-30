/*
 * cdrom/cd-queue - nocash's "CDROM - Response/Data Queueing" hardware tests
 * (psx-spx, current edition; not in the copy vendored here), rebuilt so this
 * core can be graded against them line by line.
 *
 * Each case: Setloc 00:02:00 + ReadN, take the first INT1, then a mix of
 * delays (no interrupt handled) and commands (GetlocL, Pause), then print every
 * response in the order it arrives: INT1 with the sector's header (whole-sector
 * mode 20h, so the data FIFO starts with MM:SS:FF), INT3 with a GetlocL's
 * position or a Pause's stat, INT2 with the Pause's second stat.
 *
 * Delay(n) is n x DELAY cycles with interrupts ignored. nocash does not give
 * his unit; his first case (a delay, then INT1 for 0:2:1 = the oldest, then
 * 0:2:6 = the newest) fixes it at about five single-speed sectors, so every
 * case is run at DELAY = 5.3 sectors and again at 4.3 and 6.3, which moves the
 * newest sector by one either way without changing what is being tested.
 *
 * The logs to match are quoted beside each case below.
 *
 * ON THIS CORE SINCE M231 (the eight-slot ring): A-F, H-K and N read as his do,
 * give or take the newest sector, which moves with the delay unit; C, G and K
 * now read 0:2:9 where the slot is overwritten. What still differs, and why
 * (STATUS M231): G delivers GetlocL's INT3 before the newest INT1, the main
 * loop's race, which DuckStation and mednafen settle opposite ways; L and M are
 * two commands written back to back, psx-spx's busy flag (A3).
 *
 * O AND P (2026-09-29, M266's rule; no nocash log). Case B again, but the
 * oldest INT1 is read in PART - 3 bytes, then Syphon Filter 2's 2,060 of 2,340 -
 * with BFRD still set when it is acknowledged. Every case A-N drops BFRD first,
 * which is why none of them told v0.265 from v0.266. READ IT AGAINST B IN THE
 * SAME RUN: O's third INT1 equal to B's third means the waiting newest sector
 * was announced after the read (DuckStation since 2024, 859517526; this core
 * since M266); one sector past B's means it was lost (M241's rule, this core
 * v0.241-v0.265). Here, v0.267: O and P read B's sector at every delay unit
 * (0:2:5, 0:2:4, 0:2:6); v0.265: one past it (0:2:6, 0:2:5, 0:2:7). A-N are
 * identical on the two.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }

static uint32_t clk8; static uint16_t lastT;
static inline uint32_t now() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; return clk8 * 8; }
static uint32_t DELAY;
static void delay(int n) { uint32_t t0 = now(); while (now() - t0 < (uint32_t)n * DELAY) { } }

/* Every wait gives up after ~2 seconds, so a response that never comes is reported, not hung on. */
static uint8_t waitAny() { uint32_t t0 = now(); for (;;) { uint32_t t = now(); uint8_t f = irqFlag(); if (f) return f; if (t - t0 > 70000000) return 0; } }
static void simple(uint8_t cmd) { command(cmd); while (irqFlag() != 3) now(); drain(); ackAll(); }

static char line[160]; static int lp;
static void out(const char* s) { while (*s && lp < 150) line[lp++] = *s++; }
static void outHex(uint8_t v) { const char* h = "0123456789ABCDEF"; line[lp++] = h[v >> 4]; line[lp++] = h[v & 15]; }

/* Handle whatever interrupt is next and describe it. */
static void process() {
    uint8_t f = waitAny();
    if (f == 0) { out(" (none)"); return; }
    if (f == 1) {
        want(true);
        uint8_t m = data(), s = data(), fr = data();
        want(false);
        drain(); ackAll();
        out(" INT1:"); outHex(m); out(":"); outHex(s); out(":"); outHex(fr);
    } else if (f == 3) {
        uint8_t r[8]; int n = 0; while (hasResp() && n < 8) r[n++] = resp(); drain(); ackAll();
        if (n >= 8) { out(" INT3:getloc "); outHex(r[0]); out(":"); outHex(r[1]); out(":"); outHex(r[2]); }
        else { out(" INT3:stat "); outHex(r[0]); }
    } else if (f == 2) {
        uint8_t st = resp(); drain(); ackAll(); out(" INT2:stat "); outHex(st);
    } else { uint8_t st = resp(); drain(); ackAll(); out(" INT"); outHex(f); out(" "); outHex(st); }
}

/* M266's case: the delivered sector read in PART, with BFRD still set when its INT1 is acknowledged and dropped
 * only after - as Syphon Filter 2's briefing stream reads (2,060 of each 2,340 bytes). */
static void processHeld(int n) {
    uint8_t f = waitAny();
    if (f != 1) { out(" (not INT1)"); drain(); ackAll(); return; }
    want(true);
    uint8_t m = data(), s = data(), fr = data();
    for (int i = 3; i < n; i++) data();
    drain(); ackAll();
    want(false);
    out(" INT1:"); outHex(m); out(":"); outHex(s); out(":"); outHex(fr); out("(part-read)");
}

static void start() {
    param(0x00); param(0x02); param(0x00); simple(0x02);           /* Setloc 00:02:00 */
    command(0x06); while (irqFlag() != 3) now(); drain(); ackAll(); /* ReadN */
    process();                                                       /* INT1 for 0:2:0 */
}
static void stopRead() { command(0x09); for (int i = 0; i < 12; i++) { uint8_t f = waitAny(); drain(); ackAll(); if (f == 2) break; } delay(1); ackAll(); drain(); }
static void begin(const char* name) { lp = 0; out(name); out(":"); }
static void end() { line[lp] = 0; printf("%s\n", line); stopRead(); }

int main() {
    printf("\ncdrom/cd-queue\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    command(0x0A); while (irqFlag() != 3) now(); drain(); ackAll(); while (irqFlag() != 2) now(); drain(); ackAll();
    simple(0x0C);
    param(0x20); simple(0x0E);                                      /* whole sectors, single speed */

    static const uint32_t units[3] = { 2395000, 1943000, 2846000 };  /* 5.3, 4.3, 6.3 sectors */
    for (int u = 0; u < 3; u++) {
        DELAY = units[u];
        printf("--- DELAY %d cycles\n", DELAY);
        /* nocash: 0:2:0, 0:2:1, 0:2:2, 0:2:3 */
        begin("A no delay"); start(); process(); process(); process(); end();
        /* nocash: 0:2:0, delay(1), 0:2:1 (oldest), 0:2:6 (newest), 0:2:7 */
        begin("B delay1"); start(); delay(1); process(); process(); process(); end();
        /* nocash: 0:2:0, delay(2), 0:2:9 (oldest/overwritten), 0:2:11, 0:2:12 */
        begin("C delay2"); start(); delay(2); process(); process(); process(); end();
        /* nocash GetlocL: INT3 getloc 0:2:0, INT1 0:2:1, 0:2:2, 0:2:3 */
        begin("D getloc"); start(); command(0x10); process(); process(); process(); process(); end();
        /* nocash: delay BEFORE getloc: INT1 0:2:1, INT3 getloc 0:2:6, INT1 0:2:6, 0:2:7 */
        begin("E delay,getloc"); start(); delay(1); command(0x10); process(); process(); process(); process(); end();
        /* nocash: delay AFTER getloc: INT3 getloc 0:2:0, INT1 0:2:5, 0:2:6, 0:2:7 */
        begin("F getloc,delay"); start(); command(0x10); delay(1); process(); process(); process(); process(); end();
        /* nocash: before and after: INT1 0:2:9, INT1 0:2:11, INT3 getloc 0:2:12, INT1 0:2:12, 0:2:13 */
        begin("G delay,getloc,delay"); start(); delay(1); command(0x10); delay(1); process(); process(); process(); process(); process(); end();
        /* nocash Pause: INT3 stat 22, INT2 stat 02 */
        begin("H pause"); start(); command(0x09); process(); process(); end();
        /* nocash: delay BEFORE pause: INT1 0:2:1, INT3 stat 22, INT2 stat 02 */
        begin("I delay,pause"); start(); delay(1); command(0x09); process(); process(); process(); end();
        /* nocash: delay AFTER pause: INT3 stat 22, INT2 stat 02 */
        begin("J pause,delay"); start(); command(0x09); delay(1); process(); process(); end();
        /* nocash: before and after: INT1 0:2:9, INT3 stat 22, INT2 stat 02 */
        begin("K delay,pause,delay"); start(); delay(1); command(0x09); delay(1); process(); process(); process(); end();
        /* nocash double: getloc then pause at once: INT3 stat 22, INT2 stat 02 (the getloc's INT3 never comes) */
        begin("L getloc+pause"); start(); command(0x10); command(0x09); process(); process(); end();
        /* nocash: delay, getloc+pause: INT1 0:2:1, INT1 0:2:6, INT3 stat 22, INT2 stat 02 */
        begin("M delay,getloc+pause"); start(); delay(1); command(0x10); command(0x09); process(); process(); process(); process(); end();
        /* nocash: getloc, delay, pause: INT3 getloc 0:2:0, INT3 stat 22, INT2 stat 02 */
        begin("N getloc,delay,pause"); start(); command(0x10); delay(1); command(0x09); process(); process(); process(); end();
        /* M266 (no nocash log): a delay, then the oldest INT1 read in part with BFRD held through its acknowledge.
         * DuckStation since 2024-06-09 (859517526, "Fixes hang in loading in Syphon Filter 2/3") announces the
         * waiting newest sector after ANY data read, so it comes next; M241's rule (this core, v0.241-v0.265)
         * announced it only once the delivered sector was read to its end, which this never reaches, so the
         * newest was replaced by the next one to land. Every case above drops BFRD before it acknowledges, which is
         * why none of them told v0.265 from v0.266. */
        begin("O delay,part-read"); start(); delay(1); processHeld(3); process(); process(); end();
        begin("P delay,part-read 2060"); start(); delay(1); processHeld(2060); process(); process(); end();
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
