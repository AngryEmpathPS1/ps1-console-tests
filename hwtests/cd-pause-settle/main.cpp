/*
 * cdrom/cd-pause-settle - is cd-pause's short double-speed cell a speed change or the radius? (the Fable CD session, 2026-11-23)
 * WHY: cd-pause's "2x lba=16 first INT1" cell - a Pause right after a read's first INT1 at double speed near the disc's start - reads
 * 1.128M on the SCPH-9001 and 1.128M on the second SCPH-5501 (Doom, x4 each, 2026-11-19), and the model 2.4-3.4% short of both
 * (v0.377.374 and with land_inflight alike; experiments/scex/pausecmp.js), where every other first-INT1 cell is within ~1%. It
 * is also the FIRST cell measured after Setmode 80h, and nocash's loop (cddrivetest, the KNOWN-OPEN beside v0.377.375) misses the
 * same way at 00:02:00. Either the spindle is still speeding up when it runs, or the motor runs slow at the inner radius at
 * double speed. The model has neither: its LBA 16 cell reads 1.098M from 0 to 3 s after the Setmode (scratch/scex/pause2xsettle.js).
 * WHAT IT DOES: Init, Demute, Setmode 00h and 1 s; then cells of 8 Pauses, each right after a read's first INT1 (cd-pause's own
 * onePause): "2x 16 at once" (Setmode 80h, then at once), "2x 16 after 3 s" (3 s later), "2x mid" (the disc's middle), "2x 16
 * after mid" (back at once), "1x 16 at once" (Setmode 00h, then at once), "1x 16 after 3 s". Each value is the Pause's INT3
 * acknowledge to its INT2 in CPU cycles, then min / avg / max.
 * READ IT LIKE THIS: a speed change shows as "at once" cells apart from the "after 3 s" ones; the radius as every 2x 16 cell
 * alike and long against the model. No unlock. SAFE FOR THE DRIVE. About 30 s.
 */
#define CDPAUSE_NO_MAIN
#include "../cd-pause/main.cpp"

static void cell(const char* label, uint32_t lba) {
    uint32_t mn = 0xFFFFFFFF, mx = 0, sum = 0, n = 0;
    printf("%s lba=%6d:", label, lba);
    for (int run = 0; run < 8; run++) {
        uint32_t ack = 0;
        const uint32_t v = onePause(lba, 0, &ack, 1);
        printf(" %d", v);
        if (v) { mn = v < mn ? v : mn; mx = v > mx ? v : mx; sum += v; n++; }
    }
    if (n) printf("   min %d avg %d max %d\n", mn, sum / n, mx);
    else printf("   (Pause refused every time)\n");
}
static void wait(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

int main() {
    printf("\ncdrom/cd-pause-settle\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                  /* Demute */
    const uint32_t total = discSectors();
    if (total == 0) {
        printf("disc: none (GetTD gave 0) - nothing to time\n");
        restoreTimer(2, prev); ExitCriticalSection(); printf("\nDone.\n"); for (;;) {}
    }
    command(0x13); waitInt(3); resp(); resp();                                     /* GetTN: the data track's end, as cd-pause */
    const uint8_t lastTrack = unbcd(resp()); drain(); ackAll();
    uint32_t end = total;
    if (lastTrack >= 2) {
        param(bcd(2)); command(0x14); waitInt(3);
        resp(); uint8_t mm = unbcd(resp()), ss = unbcd(resp()); drain(); ackAll();
        end = (mm * 60 + ss) * 75;
    }
    printf("disc: %d sectors, its data to %d\n", total, end);
    const uint32_t S = 33868800;
    setmode(0x00); wait(S);
    setmode(0x80); cell("2x 16 at once     ", 16);
    wait(3 * S);   cell("2x 16 after 3 s   ", 16);
                   cell("2x mid            ", end / 2);
                   cell("2x 16 after mid   ", 16);
    setmode(0x00); cell("1x 16 at once     ", 16);
    wait(3 * S);   cell("1x 16 after 3 s   ", 16);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
