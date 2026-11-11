/*
 * cdrom/cd-audiodisc - what the drive answers about a MUSIC CD: GetID, ReadN with and without CD-DA mode (each waited up to 15 s: the first console run saw nothing in 5), GetlocL during Play, and
 * SeekL (2026-11-11, main session).
 *
 * WHY. v0.377.133 made an audio CD an audio CD (STATUS "v0.377.133: an audio CD is an audio CD"): the HLE answers psx-spx's "Audio
 * Disk" row and the drive programs, run as the drive, give it byte for byte - but no console has been asked. psx-spx says, for a
 * music CD (reference/docs/psx-spx.txt):
 *   6294  GetID: INT3(stat), then INT5(0Ah,90h, 00h,00h, 00h,00h,00h,00h)
 *   5994  ReadN: "Sectors on Audio CDs can be read only when CDDA is enabled via Setmode (otherwise error code 40h is returned)"
 *   6199  GetlocL "fails (with error code 80h) when playing Audio CDs"
 *   5916  SeekL "passes okay on the first response, but (after two seconds or so) the second response will return an error
 *         (stat+4,04h), and stop the drive motor... that error doesn't appear ALWAYS though"
 * Each is asked here with its bytes and its times. Needs a pressed music CD (all CD-DA) in the drive.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, every wait bounded (rule 1); Timer 2 at the system clock, every step added
 * into 32 bits. Init to its INT2, Getstat until the motor is on; GetTN, GetTD 0. Then, each printed as "<what>: INT<f> bytes ...
 * | INT<g> bytes ... after N cycles" (the first response, then the second's and its time from the first):
 *   GetID x4, 500 ms apart.
 *   Setmode 00h, Setloc 10:00:00, ReadN - the response that follows its INT3 (INT1 or INT5) and its bytes; Pause.
 *   Setmode 01h (CD-DA), the same ReadN; Pause.
 *   Setloc 10:00:00, Play; 1 s; GetlocL, GetlocP; Pause.
 *   Setmode 00h, Setloc 20:00:00, SeekL, both responses (up to 10 s), then Getstat; SeekP the same, x2 each.
 * Last Init, so the drive is left spun up for what follows.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-audiodisc` with a music CD's image.
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

int main() {
    printf("\ncdrom/cd-audiodisc\nWhat the drive answers about a music CD: GetID, ReadN, GetlocL, SeekL. No assertions.\n\n");
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
    ask("GetTN", 0x13, 0, 0, false, 0);
    const uint8_t z[1] = { 0 };
    ask("GetTD 0 (the lead-out)", 0x14, z, 1, false, 0);

    for (int k = 0; k < 4; k++) { ask("GetID", 0x1A, 0, 0, true, MS(5000)); idle(MS(500)); }

    const uint8_t m0[1] = { 0x00 }, mDA[1] = { 0x01 };
    const uint8_t loc10[3] = { 0x10, 0x00, 0x00 }, loc20[3] = { 0x20, 0x00, 0x00 };
    ask("Setmode 00h", 0x0E, m0, 1, false, 0);
    ask("Setloc 10:00:00", 0x02, loc10, 3, false, 0);
    ask("ReadN, CD-DA off", 0x06, 0, 0, true, MS(15000));
    pauseQuiet();
    ask("Setmode 01h (CD-DA)", 0x0E, mDA, 1, false, 0);
    ask("Setloc 10:00:00", 0x02, loc10, 3, false, 0);
    ask("ReadN, CD-DA on", 0x06, 0, 0, true, MS(15000));
    pauseQuiet();

    ask("Setloc 10:00:00", 0x02, loc10, 3, false, 0);
    ask("Play", 0x03, 0, 0, false, 0);
    idle(MS(1000));
    while (irqFlag()) { drain(); ackAll(); idle(MS(2)); }   /* report INT1s, if CD-DA mode's report bit were on: not here */
    ask("GetlocL, playing", 0x10, 0, 0, false, 0);
    ask("GetlocP, playing", 0x11, 0, 0, false, 0);
    pauseQuiet();

    for (int k = 0; k < 2; k++) {
        ask("Setmode 00h", 0x0E, m0, 1, false, 0);
        ask("Setloc 20:00:00", 0x02, loc20, 3, false, 0);
        ask("SeekL", 0x15, 0, 0, true, MS(10000));
        ask("Getstat after SeekL", 0x01, 0, 0, false, 0);
        ask("Setloc 20:00:00", 0x02, loc20, 3, false, 0);
        ask("SeekP", 0x16, 0, 0, true, MS(10000));
        ask("Getstat after SeekP", 0x01, 0, 0, false, 0);
        idle(MS(500));
    }
    ask("Init (left spun up)", 0x0A, 0, 0, true, MS(10000));
    ExitCriticalSection();
    printf("\ncdrom/cd-audiodisc: Done.\n");
    return 0;
}
