/*
 * cdrom/cd-lockstate - whether the drive was found UNLOCKED, read off a music CD: ReadN as found, after a lone 56h (which relocks),
 * and after the secret unlock (2026-11-12, main session).
 *
 * WHY. cd-audiodisc on the second SCPH-5501 (Metallica ReLoad, 2026-11-11): a ReadN without CD-DA acked and failed INT5 06 04
 * after 8.6 s, where psx-spx (line 5994) has error 40h, and the drive's own program, SC430930, run here refuses at once with 40h.
 * The program's ReadN (309Dh) refuses an unlicensed disc - 90h bit 7, what GetID reports as 90h - unless 9Fh bit 7 is set, the
 * secret unlock's last step (3A86h); 9Fh is cleared only by the drive's reset and by a 56h with no unlock before it (3A7Dh ->
 * 3A8Fh). Unlocked first, the program answers the music CD's ReadN with INT3, then INT5 06 04 after 8,543-8,582 ms: the console's
 * row (scratch/scratch_musicreadn2.js, scratch/scratch_relock.js). So the console's drive was unlocked when it ran: tools/consolerun
 * unlocks a U/C drive for a burned disc (2026-11-07 on this unit), and psx-spx says only the drive's Reset or a console RESET
 * undoes it. This asks the drive which, on the console: the same ReadN as found, relocked, and unlocked.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (rule 1). Init to its INT2, Getstat until
 * the motor is on, GetID (the disc: 90h = unlicensed / audio, else this test says nothing). Then, Setmode 00h, Setloc 10:00:00 and
 * ReadN, with the response after its INT3 (up to 12 s), Pause - three times: as found; after a lone 56h; after the unlock (50h-56h
 * with the region from Test 19h,22h). The drive is left UNLOCKED, as cd-audiodisc found it.
 * READ IT LIKE THIS. As found INT3 + 06 04 at ~8.6 s, relocked INT5 40h at once, unlocked INT3 + 06 04: it was unlocked, and
 * psx-spx's 40h is the locked drive's answer. As found 40h at once: it was locked, and cd-audiodisc's row wants another reason.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js cd-lockstate --disc=music` (PREDICTED.txt).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

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
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint8_t rb[16]; static int nrb;
/* psx-spx's flag polling (rule 1): a first 1 or 4 is read again before it is taken */
static uint8_t take(uint32_t limit) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    nrb = 0; while (nrb < 16 && hasResp()) rb[nrb++] = resp();
    drain();
    ackAll();
    return f;
}
static void pr(const uint8_t *b, int n) { for (int i = 0; i < n; i++) printf(" %02X", b[i]); }

/* A command; its first response printed; with `second`, the next response (INT1/2/5: whatever comes, up to `lim2`) and its time
 * from the first. Returns the second's flag (or the first's without `second`). */
static uint8_t ask(const char *what, uint8_t c, const uint8_t *p, int np, bool second, uint32_t lim2) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    const uint8_t f = take(MS(3000));
    uint8_t b1[16]; const int n1 = nrb; for (int i = 0; i < n1; i++) b1[i] = rb[i];
    const uint32_t t1 = now();
    printf("  %-34s INT%u", what, f); pr(b1, n1);
    uint8_t g = f;
    if (second && f == 3) {
        g = take(lim2);
        const uint32_t dt = now() - t1;
        printf(" | INT%u", g); pr(rb, nrb);
        if (g) printf(" after %u cycles", (unsigned)dt); else printf(" (none within the wait)");
    }
    printf("\n");
    return g;
}
static void pauseQuiet() {
    command(0x09);
    uint8_t f = take(MS(3000));
    if (f == 1) { const uint32_t t0 = now(); do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000)); }
    if (f == 3) take(MS(5000));
    idle(MS(50)); while (irqFlag()) { drain(); ackAll(); idle(MS(5)); }
}


static void readn(const char *what) {
    const uint8_t m0[1] = { 0x00 }, loc10[3] = { 0x10, 0x00, 0x00 };
    ask("Setmode 00h", 0x0E, m0, 1, false, 0);
    ask("Setloc 10:00:00", 0x02, loc10, 3, false, 0);
    ask(what, 0x06, 0, 0, true, MS(12000));
    pauseQuiet();
}

int main() {
    printf("\ncdrom/cd-lockstate\nWhether the drive was found unlocked: a music CD's ReadN as found, relocked by a lone 56h, unlocked. No assertions.\n\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    ask("Init", 0x0A, 0, 0, true, MS(10000));
    uint8_t gs = 0; int polls = 0;
    for (const uint32_t t0 = now(); now() - t0 < MS(15000); polls++) {
        command(0x01); if (take(MS(3000)) == 3 && nrb) gs = rb[0];
        if ((gs & 0x02) && !(gs & 0xE0)) break;
        idle(MS(50));
    }
    printf("  Getstat %02X after %d polls\n", gs, polls);
    const uint8_t gid = ask("GetID", 0x1A, 0, 0, true, MS(5000));
    if (gid == 5 && nrb > 1 && (rb[1] & 0x40)) {                /* 40h: no disc (psx-spx GetID "No Disk") */
        printf("  NO DISC in the drive: this run says nothing about the lock.\n");
        ExitCriticalSection();
        printf("\ncdrom/cd-lockstate: Done.\n");
        return 0;
    }
    if (!(gid == 5 && nrb > 1 && (rb[1] & 0x10))) printf("  (GetID does not call it an audio CD: the rows below are not the music CD's)\n");

    readn("ReadN, as found");
    ask("56h alone (relocks)", 0x56, 0, 0, true, MS(1000));
    readn("ReadN, after 56h");

    /* the unlock, cd-unlock's way: the region from Test 19h,22h ("for U/C" -> "of America", "for Europe" -> "(Europe)") */
    const uint8_t t22[1] = { 0x22 };
    for (int i = 0; i < 1; i++) param(t22[i]);
    command(0x19);
    const uint8_t fr = take(MS(3000));
    char reg[17] = { 0 }; for (int i = 0; i < nrb && i < 16; i++) reg[i] = (char)rb[i];
    const char *str = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;
    int n5 = 0;
    if (str) {
        static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
        for (int k = 0; k < 7; k++) {
            const char *s = (k == 5) ? str : S[k];
            if (s) for (int i = 0; s[i] && i < 16; i++) param((uint8_t)s[i]);
            command(0x50 + k);
            const uint8_t f = take(MS(3000));
            if (f == 5 && nrb > 1 && rb[1] == 0x40) n5++;
        }
    }
    printf("  region INT%u \"%s\": %s; unlock answers INT5 40h %d of 7\n", fr, reg, str ? str : "no unlock for this region", n5);
    readn("ReadN, after the unlock");

    ask("Init (left spun up, unlocked)", 0x0A, 0, 0, true, MS(10000));
    ExitCriticalSection();
    printf("\ncdrom/cd-lockstate: Done.\n");
    return 0;
}
