/*
 * cdrom/cd-ack-times - how long each command takes to answer, when a Pause
 * after a read is refused, and what an acknowledge does to the next interrupt.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, CD rows C1, C2, C4, C13, C37 and C56).
 *   FIRST RESPONSES. nocash timed Getstat on a PAL PSone (average 0C4E1h,
 *     04A73h..3115Bh; stopped 05CF4h) and wrote "Timings for most other
 *     commands should be similar"; ps1-tests' console timed Nop, Mute,
 *     Demute, Setloc, Setmode and Init. Every other command's first response
 *     here is Getstat's (row C2: "nothing measures"), and the spread is M122's
 *     sub-CPU mainloop: a poll period with jitter and 15 slow maintenance
 *     cycles a second (rows C1, C4), fitted to those two consoles.
 *   THE PAUSE REFUSAL (row C56). A Pause written while a ReadN or SeekL is
 *     still seeking is refused with INT5 80h and the read goes on - DuckStation
 *     ("verified with hardware tests") - judged when the sub-CPU takes the
 *     command, and accepted if the read's first sector lands before that (Tomb
 *     Raider III's loop, M181). Where the window ends on a console is not
 *     recorded anywhere.
 *   THE HOLD-OFF (row C13). After an acknowledge, the next interrupt waits
 *     until 1,000 cycles after it (DuckStation's MINIMUM_INTERRUPT_DELAY, "the
 *     real console does something similar anyway"; measured by nobody).
 *   A COMMAND BEHIND A PENDING INTERRUPT (row C37). The sub-CPU takes a new
 *     command only with no interrupt pending (psx-spx); here its first
 *     response then counts its whole delay again from the acknowledge
 *     (DuckStation's UpdateCommandEvent).
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system
 * clock unwrapped in software. Needs a data disc (any game).
 *   FIRST RESPONSE: 64 of each command, a pseudo-random 0-2 ms apart, the time
 *     from the command write to its first interrupt: min, average, max and a
 *     histogram in 16,384-cycle steps. Getstat, Getparam, GetTN, GetTD,
 *     GetlocL, GetlocP, Test 20h, Setloc, Setmode (no speed change),
 *     Setfilter, Mute, Demute, GetID (and its second response), Pause when
 *     paused (and its second), SeekL to where the head is (and its second),
 *     and ReadN (then a Pause).
 *   PAUSE AFTER READN: the head parked at 00:02:16, Setloc 00:06:16 and ReadN;
 *     D ms after its INT3 a Pause; the Pause's answer (INT3, or INT5 and its
 *     byte), when, and how many sectors came first; three times each D. The
 *     same after SeekL.
 *   HOLD-OFF: a single-speed read; after a sector's INT1 a Getstat is written
 *     D cycles before the next sector is due, its INT3 acknowledged at once,
 *     and the next INT1 timed from that acknowledge. D = 10,000..90,000.
 *   PENDING: Getstat's INT3 left unacknowledged, Getparam written, then after
 *     X ms the acknowledge: Getparam's INT3 timed from it. X = 0.1, 1, 5 ms.
 *
 * READ IT LIKE THIS. Cycles at 33,868,800 a second. A command whose first
 * responses sit well above Getstat's is doing work before it answers; the
 * PAUSE rows' last D with INT5 is the refusal window; the HOLD-OFF's smallest
 * gaps are the console's minimum; the PENDING rows near Getstat's first
 * response say the delay restarts at the acknowledge, near zero that the
 * command was taken while the interrupt was pending.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js cd-ack-times --disc
 * --frames=12000 --quiet=10000`, 51 s here; --quiet 6000 until v0.377.128, when a Pause refused inside a seek
 * took the seek's INT2 away as on the consoles (hwtests/cd-seek-pause) and pauseAll() waits out its 2 s for every
 * refused SeekL row - ~84 s without a line, as the SCPH-7001's own run took 145.6 s):
 *   FIRST RESPONSE: every command Getstat's shape, min 24,084-27,596, average
 *     43,378-50,762, max 80,881-157,901 (the mainloop's slow cycles); Setloc a
 *     little later (min 34,313, average 55,918). GetID's INT2 18,896-18,964
 *     (nocash's 4A00h), Pause-when-paused's 7,628-7,700 (nocash's 1DF2h), a
 *     SeekL to where the head is 0.9-2.9M.
 *   PAUSE: refused (INT5 80h) at every D to 60 ms after a ReadN and to 50 ms
 *     after a SeekL - the 55 ms seek - and accepted from 80 and 60 ms, after
 *     the read's first sectors or the seek's INT2.
 *   HOLD-OFF: INT1 every 452,219 at single speed; where a sector came after
 *     its due time, the acknowledge -> INT1 gap is at least 1,131 (the 1,000
 *     and the reads' own cost). A few sectors are skipped whole (a gap of
 *     ~400,000): a sector that landed while the INT3 was pending.
 *   PENDING: Getparam's INT3 28,771-84,207 after the acknowledge at every X,
 *     a first response's whole delay counted again.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

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
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }
static void until(uint32_t t) { while ((int32_t)(now() - t) < 0) {} }

struct Got { uint8_t flag, b0, b1; uint32_t t; };
/* The next interrupt (up to `limit` cycles): which, its first two bytes, when; drained and acknowledged. */
static Got take(uint32_t limit = MS(3000)) {
    Got g = { 0, 0, 0, 0 };
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        /* An old board's flag can read 1 before 3, or 4 before 5 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's
         * cd-irq-glitch: once in 3,000): such a first read is read again, or the INT3 is acknowledged as an INT1 and a
         * later wait for it never ends (cd-ack-times hung the 1001 so, 2026-10-09). The time is the first read's. */
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    if (hasResp()) g.b0 = resp();
    if (hasResp()) g.b1 = resp();
    drain();
    ackAll();
    return g;
}
/* The next interrupt of kind `k` (an INT5 or a timeout ends it too), acknowledging the rest; `skipped` counts them. */
static Got takeKind(uint8_t k, uint32_t limit, int* skipped = 0) {
    for (;;) {
        const Got g = take(limit);
        if (!g.flag || g.flag == k || g.flag == 5) return g;
        if (skipped) (*skipped)++;
    }
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); command(0x02); take();
}
static void simple(uint8_t c) { ackAll(); command(c); take(); }
/* Pause, taking whatever comes first, until its INT2. */
static void pauseAll() { ackAll(); command(0x09); for (int i = 0; i < 24; i++) { const Got g = take(MS(2000)); if (!g.flag || g.flag == 2) break; } idle(MS(1)); ackAll(); drain(); }
static void park() { setloc(16); ackAll(); command(0x15); take(); takeKind(2, MS(5000)); idle(MS(20)); }

static uint32_t rng = 0x12345678u;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define NS 64
struct Stat { uint32_t v[NS]; int n; };
static void report(const char* name, const Stat& s) {
    uint32_t lo = 0xFFFFFFFFu, hi = 0, sum = 0;
    int hist[12] = { 0 };
    for (int i = 0; i < s.n; i++) {
        const uint32_t v = s.v[i];
        lo = v < lo ? v : lo; hi = v > hi ? v : hi; sum += v;
        const uint32_t b = v >> 14;
        hist[b < 11 ? b : 11]++;
    }
    printf("  %-16s min %7u  avg %7u  max %7u  |", name, (unsigned)lo, (unsigned)(s.n ? sum / s.n : 0), (unsigned)hi);
    for (int b = 0; b < 12; b++) printf(" %2d", hist[b]);
    printf("\n");
}

/* One command 64 times, each timed from its write to its first interrupt; `second` also times the second. */
static Stat first_, second_;
static void timeCmd(uint8_t c, const uint8_t* p, int np, int second, void (*before)(), void (*after)()) {
    first_.n = second_.n = 0;
    for (int i = 0; i < NS; i++) {
        if (before) before();
        idle(rnd() % MS(2));
        ackAll();
        for (int k = 0; k < np; k++) param(p[k]);
        const uint32_t t0 = now();
        command(c);
        const Got a = take();
        first_.v[first_.n++] = a.t - t0;
        if (second && a.flag == 3) {
            const Got b = takeKind(second, MS(5000));
            second_.v[second_.n++] = b.t - a.t;
        }
        if (after) after();
    }
}
static void setloc216() { setloc(16); }
static void waitSector() { takeKind(1, MS(3000)); pauseAll(); }

struct PauseRow { uint8_t flag, b1; uint32_t at; int sectors; uint32_t second; };
static PauseRow pr[2][14][3];
static const uint32_t PAUSE_D[] = { 0, 1, 2, 5, 10, 15, 20, 30, 40, 50, 60, 80, 100, 150 };

struct HoldRow { int32_t d; uint8_t order; uint32_t int3, gap; int32_t late; };
static HoldRow hr[120];
static uint32_t holdP;
struct PendRow { uint32_t x; uint8_t flag; uint32_t after; };
static PendRow pd[24];

int main() {
    printf("\ncdrom/cd-ack-times\n");
    printf("First responses, the Pause refusal, the hold-off and a command behind a pending interrupt. Needs a disc.\n");
    printf("About a minute; nothing prints until each part is done.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    ackAll(); command(0x0A); take(); takeKind(2, MS(5000));     /* Init */
    simple(0x0C);                                               /* Demute */
    ackAll(); param(0x00); command(0x0E); take();               /* Setmode 00h */
    park();
    setloc(16); ackAll(); command(0x06); take(); takeKind(1, MS(3000)); pauseAll();   /* a header for GetlocL */

    printf("FIRST RESPONSE, write -> first interrupt, %d each (histogram: 16,384-cycle steps, the last 180,224+):\n", NS);
    static const uint8_t p00[] = { 0x00 }, p216[] = { 0x00, 0x02, 0x16 }, p20[] = { 0x20 }, pf[] = { 0x01, 0x00 };
    timeCmd(0x01, 0, 0, 0, 0, 0); report("Getstat", first_);
    timeCmd(0x0F, 0, 0, 0, 0, 0); report("Getparam", first_);
    timeCmd(0x13, 0, 0, 0, 0, 0); report("GetTN", first_);
    timeCmd(0x14, p00, 1, 0, 0, 0); report("GetTD 0", first_);
    timeCmd(0x10, 0, 0, 0, 0, 0); report("GetlocL", first_);
    timeCmd(0x11, 0, 0, 0, 0, 0); report("GetlocP", first_);
    timeCmd(0x19, p20, 1, 0, 0, 0); report("Test 20h", first_);
    timeCmd(0x02, p216, 3, 0, 0, 0); report("Setloc", first_);
    timeCmd(0x0E, p00, 1, 0, 0, 0); report("Setmode 00h", first_);
    timeCmd(0x0D, pf, 2, 0, 0, 0); report("Setfilter", first_);
    timeCmd(0x0B, 0, 0, 0, 0, 0); report("Mute", first_);
    timeCmd(0x0C, 0, 0, 0, 0, 0); report("Demute", first_);
    timeCmd(0x1A, 0, 0, 2, 0, 0); report("GetID", first_); report("  its INT2", second_);
    timeCmd(0x09, 0, 0, 2, 0, 0); report("Pause (paused)", first_); report("  its INT2", second_);
    timeCmd(0x15, 0, 0, 2, setloc216, 0); report("SeekL (here)", first_); report("  its INT2", second_);
    timeCmd(0x06, 0, 0, 0, setloc216, waitSector); report("ReadN", first_);
    printf("\n");

    for (int kind = 0; kind < 2; kind++)                        /* PAUSE after ReadN, then after SeekL */
        for (unsigned d = 0; d < sizeof(PAUSE_D) / sizeof(PAUSE_D[0]); d++)
            for (int k = 0; k < 3; k++) {
                park();
                setloc(316);
                ackAll();
                command(kind ? 0x15 : 0x06);
                const Got a = take();
                int sectors = 0;
                const uint32_t due = a.t + MS(PAUSE_D[d]);
                while ((int32_t)(now() - due) < 0) {             /* interrupts before the Pause are taken (and counted) */
                    if (irqFlag()) { take(); sectors++; }
                }
                ackAll();
                const uint32_t tp = now();
                command(0x09);
                const Got b = takeKind(3, MS(3000), &sectors);   /* its INT3, or INT5 (a refusal) */
                PauseRow& r = pr[kind][d][k];
                r.flag = b.flag; r.b1 = b.b1; r.at = b.t - tp; r.sectors = sectors; r.second = 0;
                if (b.flag == 3) { const Got c = takeKind(2, MS(3000)); r.second = c.t - b.t; }
                else pauseAll();                                 /* refused: the read went on */
            }
    printf("PAUSE D ms after the INT3 of a ReadN (then a SeekL) from 00:02:16 to 00:06:16:\n");
    printf("  answer, its time from the Pause's write, [interrupts since the ReadN's or SeekL's INT3], and INT2's time after the INT3\n");
    for (int kind = 0; kind < 2; kind++) {
        printf("  %s\n", kind ? "SeekL:" : "ReadN:");
        for (unsigned d = 0; d < sizeof(PAUSE_D) / sizeof(PAUSE_D[0]); d++) {
            printf("    D %3u", (unsigned)PAUSE_D[d]);
            for (int k = 0; k < 3; k++) {
                const PauseRow& r = pr[kind][d][k];
                if (r.flag == 3) printf("   INT3 %6u [%d] %8u", (unsigned)r.at, r.sectors, (unsigned)r.second);
                else printf("   INT%d/%02x %6u [%d]         ", r.flag, r.b1, (unsigned)r.at, r.sectors);
            }
            printf("\n");
        }
    }
    printf("\n");

    park();                                                     /* HOLD-OFF */
    setloc(16);
    ackAll(); command(0x06); take();
    for (int i = 0; i < 4; i++) takeKind(1, MS(3000));
    uint32_t t1 = takeKind(1, MS(3000)).t, tl = t1;
    for (int i = 0; i < 16; i++) tl = takeKind(1, MS(3000)).t;
    holdP = (tl - t1) / 16;
    int nh = 0;
    for (int32_t D = 10000; D <= 90000 && nh < 120; D += 4000)
        for (int k = 0; k < 5 && nh < 120; k++) {
            const Got s = takeKind(1, MS(3000));
            const uint32_t due = s.t + holdP;
            until(due - D);
            ackAll();
            const uint32_t tw = now();
            command(0x01);
            const Got a = take();
            HoldRow& h = hr[nh++];
            h.d = D; h.int3 = a.t - tw;
            if (a.flag == 3) {
                const uint32_t ackAt = now();                   /* take() acknowledged it just now */
                const Got b = takeKind(1, MS(3000));
                h.order = 31; h.gap = b.t - ackAt; h.late = (int32_t)(b.t - due);
            } else {
                const Got b = takeKind(3, MS(3000));
                h.order = 13; h.gap = b.t - a.t; h.late = (int32_t)(a.t - due);
            }
        }
    pauseAll();
    printf("HOLD-OFF: single speed, INT1 every %u; Getstat written D before the next sector, its INT3 acknowledged at once:\n", (unsigned)holdP);
    printf("  D, order (31 = INT3 first), INT3 after the write, the INT1 after the acknowledge, the INT1 against its due time\n");
    uint32_t minGap = 0xFFFFFFFFu;
    for (int i = 0; i < nh; i++) {
        const HoldRow& h = hr[i];
        const char sg = h.late < 0 ? '-' : '+';
        printf("  D %6d  %d  %7u  %7u  %c%u\n", (int)h.d, h.order, (unsigned)h.int3, (unsigned)h.gap, sg, (unsigned)(h.late < 0 ? -h.late : h.late));
        if (h.order == 31 && h.late > 0 && h.gap < minGap) minGap = h.gap;
    }
    printf("  the smallest acknowledge -> INT1 where the INT1 came after its due time: %u\n\n", (unsigned)minGap);

    int np = 0;                                                 /* PENDING */
    static const uint32_t PX[] = { 3387, 33869, 169345 };       /* 0.1, 1, 5 ms */
    for (int x = 0; x < 3; x++)
        for (int k = 0; k < 8; k++) {
            idle(rnd() % MS(2));
            ackAll();
            command(0x01);                                      /* Getstat: its INT3 left pending */
            while (!irqFlag()) now();
            command(0x0F);                                      /* Getparam, behind it */
            idle(PX[x]);
            drain();
            ackAll();
            const uint32_t ta = now();
            const Got g = take();
            PendRow& p = pd[np++];
            p.x = PX[x]; p.flag = g.flag; p.after = g.t - ta;
        }
    printf("PENDING: Getparam written behind Getstat's unacknowledged INT3; after X cycles the acknowledge:\n");
    printf("  its interrupt, cycles after the acknowledge\n");
    for (int i = 0; i < np; i++) printf("  X %6u  INT%d %7u\n", (unsigned)pd[i].x, pd[i].flag, (unsigned)pd[i].after);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncdrom/cd-ack-times: Done.\n");
    for (;;) VSync(0);
}
