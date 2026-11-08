/*
 * cdrom/cd-speedack2 - the drive program's busy windows after a Pause, swept: a Getstat W cycles after a paused read's INT2, W from
 * 25k to 1,000k, at double speed and at single (2026-11-07).
 *
 * WHY. hwtests/cd-speedack (console-logs/2026-11-07-SCPH-7501-speedack, -SCPH-5501-u2-speedack): the first command 430k cycles
 * after a DOUBLE-speed Pause answers in up to ~110k on the 7501 (median 61-91k) and in 21-57k here (its program SC430940); after a
 * SINGLE-speed one 65-146k on the 7501 and here alike - but 21-32k on the second 5501, whose program here (SC430930) takes 65-136k.
 * The drive's own program, run alone (scratch/speedackprobe.js), says why a first answer is slow: the program's hold jumps back
 * (CX 4900) about a sector after the Pause - 214-245k at double speed, ~448k at single - and holds every answer until GFS returns
 * (41k / 64k later) and a turn of its loop. So where each console's jumps fall after a Pause, at each speed, is what differs: this
 * sweep maps them - a slow answer marks a W inside a jump.
 *
 * WHAT IT DOES. Mute; for each speed (2X: Setmode 80h; 1X: 00h) and W = 25k, 50k ... 1,000k (40 values), twice: Setmode, Setloc
 * 20:57:19, ReadN, 8 sectors, Pause to its INT2, W cycles, Getstat - its INT3's time from the write, and the stat. 160 rows, about a
 * minute. Interrupts off; every wait bounded (rule 1); a half-set flag read again. Any data disc reaching 21:00 (The X-Files Disc 1).
 * No assertions - compare against your emulator. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-speedack2 --disc=xfiles --frames=8000 --quiet=6000` (PSX_MODEL and PSX_CD_FIRMWARE the
 * console's).
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
static void bfrd(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint8_t take(uint32_t limit, uint8_t *st) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    const uint8_t s = hasResp() ? resp() : 0;
    if (st) *st = s;
    drain();
    if (f == 1) bfrd(false);
    ackAll();
    return f;
}
static uint32_t timed(uint8_t c, const uint8_t *p, int np, bool second, uint8_t *st) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t0 = now();
    command(c);
    uint8_t s = 0, f; uint32_t at = 0;
    do { f = take(MS(3000), &s); } while (f == 1 && now() - t0 < MS(3000));
    if (f == 3) at = now() - t0;
    if (st) *st = s;
    if (second && f == 3) { uint8_t s2; do { f = take(MS(10000), &s2); } while (f == 1 && now() - t0 < MS(10000)); }
    return at;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

#define N_W 40
static uint32_t ack[2][2][N_W]; static uint8_t stat[2][2][N_W];

int main() {
    printf("\ncdrom/cd-speedack2\nThe drive's busy windows after a Pause: a Getstat W cycles after a paused read's INT2.\n");
    printf("Cycles at the system clock (1 ms = 33,869) from the Getstat's write to its INT3, its stat beside it. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t st;
    timed(0x0B, 0, 0, false, &st);                          /* Mute */
    for (int pass = 0; pass < 2; pass++)
        for (int sp = 0; sp < 2; sp++)
            for (int w = 0; w < N_W; w++) {
                const uint8_t pm[1] = { (uint8_t)(sp ? 0x00 : 0x80) }; timed(0x0E, pm, 1, false, &st);
                const uint8_t pl[3] = { bcd(20), bcd(57), bcd(19) }; timed(0x02, pl, 3, false, &st);
                command(0x06);
                int n = 0; const uint32_t t0 = now();
                while (n < 8 && now() - t0 < MS(3000)) { const uint8_t f = take(MS(1000), &st); if (f == 1) n++; else if (f == 0) break; }
                timed(0x09, 0, 0, true, &st);
                idle(25000u * (uint32_t)(w + 1));
                ack[pass][sp][w] = timed(0x01, 0, 0, false, &stat[pass][sp][w]);
            }
    timed(0x0C, 0, 0, false, &st);                          /* Demute */
    ExitCriticalSection();
    for (int sp = 0; sp < 2; sp++)
        for (int w = 0; w < N_W; w++)
            printf("%s W %4uk: %u (%02X) %u (%02X)\n", sp ? "1X" : "2X", 25u * (unsigned)(w + 1),
                   (unsigned)ack[0][sp][w], stat[0][sp][w], (unsigned)ack[1][sp][w], stat[1][sp][w]);
    printf("cdrom/cd-speedack2: Done.\n");
    return 0;
}
