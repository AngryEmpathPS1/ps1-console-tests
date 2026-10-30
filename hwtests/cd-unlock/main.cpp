/*
 * cdrom/cd-unlock - NOT A TEST: the drive's secret unlock before CD tests, for a disc of another region (2026-10-19).
 *
 * WHY. The SCPH-7502 (PAL, PU-22 as the 7501) comes on its console day before a European disc can: The X-Files (USA)
 * Disc 1, the disc every other console ran, is what there is. A European drive reads no data off an American disc - the
 * SCEx string in its lead-in is SCEA where the drive wants SCEE - until the secret unlock (psx-spx "CDROM - Secret
 * Unlock Commands": 50h, 51h "Licensed by", 52h "Sony", 53h "Computer", 54h "Entertainment", 55h <region>, 56h; the
 * region from Test 19h,22h - "of America" on a US drive, "(Europe)" on a European one; drives vC1 and up; Japanese
 * drives ignore it). psx-spx: unlocked, ReadN/ReadS read "imported CDROMs from other regions", and the state survives
 * opening the lid but not the drive's Reset (1Ch) or the HCRISD bit - nor a console RESET, which resets the controller.
 * So tools/consolerun.js --unlock sends this through the runner before EVERY program that reads the disc (a revived
 * console comes back locked), and the 7502 then reads the 7501's own disc, sector for sector: the twin boards compare
 * with only PAL between them. Not in CONSOLE-RUN-SHEET.md's table: no console owes a log of it, nothing grades it.
 * What the unlock cannot change (psx-spx): GetID still calls the disc unlicensed, and Test 19h,05h's SCEx counters.
 *
 * WHAT IT DOES. Interrupts off (the kernel's CD handler would take the acknowledges), every wait bounded, a first flag
 * of 1 or 4 read again before it is taken (CLAUDE.md's console rule 1). Test 19h,22h; the seven unlock commands, each to
 * its answer (INT5 11h,40h, unlocked or not - psx-spx); then the proof: Init to its INT2, Setmode 00h, Setloc 00:02:16 (LBA 16, the
 * volume descriptor), ReadN to its first INT1 - or its INT5, which a locked drive answers - the sector's first six bytes
 * (01h "CD001" on any PlayStation disc), Pause to its INT2, and - if the motor was off when it began - Stop to its INT2,
 * so the next test starts with the drive as it would have been. Prints one line and "Done.".
 *
 * Run here first: `node hwtests/run.js cd-unlock --disc` (this emulator's drive answers the seven with INT5 11h,40h and
 * changes nothing - a disc image carries no SCEx wobble, so every data disc reads; region "for U/C").
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
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next flag within `limit` cycles (0: none); up to `n` response bytes into r (the rest drained); acknowledged. */
static uint8_t take(uint32_t limit, uint8_t *r, int n) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) {
            if (f == 1 || f == 4) { idle(200); f = irqFlag(); }   /* half of 3 or 5? read it again */
            break;
        }
        if (now() - t0 > limit) return 0;
    }
    for (int i = 0; i < n; i++) r[i] = hasResp() ? resp() : 0;
    drain();
    if (f != 1) ackAll();                                     /* an INT1's sector is read before it is acknowledged */
    return f;
}

int main() {
    printf("\ncdrom/cd-unlock: the drive's secret unlock (not a test)\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t r[16];
    command(0x01); take(MS(3000), r, 1);
    const bool motorWas = (r[0] & 2) != 0;                    /* restored at the end: the next test starts as it would have */

    /* the drive's region (psx-spx Test 19h,22h): "for U/C", "for Europe", "for Japan", "for NETNA" ... */
    char reg[17] = { 0 };
    param(0x22); command(0x19);
    const uint8_t fr = take(MS(3000), (uint8_t *)reg, 16);
    const char *str = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;

    int n5 = 0;
    if (str) {
        static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
        for (int k = 0; k < 7; k++) {
            const char *s = (k == 5) ? str : S[k];
            if (s) for (int i = 0; s[i] && i < 16; i++) param((uint8_t)s[i]);
            command(0x50 + k);
            const uint8_t f = take(MS(3000), r, 2);
            if (f == 5 && r[1] == 0x40) n5++;
        }
    }

    /* the proof: the volume descriptor at LBA 16 read, or refused - after an Init to its INT2, as every CD test begins,
     * since the BIOS's own look at a disc it refuses can leave the drive anywhere. The drive's own program keeps the
     * unlock through Init and loses it to Reset (1Ch), as psx-spx says (SC430939 alone, The X-Files (USA):
     * scratch/scratch_unlockprobe.js - unlocked, Init, ReadN: INT1s; unlocked, Reset, Init, ReadN: none). */
    command(0x0A);
    if (take(MS(3000), r, 1) == 3) take(MS(10000), r, 1);
    param(0x00); command(0x0E); take(MS(3000), r, 1);
    param(0x00); param(0x02); param(0x16); command(0x02); take(MS(3000), r, 1);
    command(0x06);
    const uint8_t fa = take(MS(3000), r, 1);
    uint8_t s1[2] = { 0, 0 }, d[6] = { 0 };
    const uint8_t f1 = fa == 3 ? take(MS(8000), s1, 2) : 0;
    if (f1 == 1) {
        idx(0); *CD3 = 0x80;                                   /* BFRD: the sector into the data FIFO */
        for (int i = 0; i < 6; i++) d[i] = *CD2;
        ackAll();
    }
    command(0x09);
    uint8_t p = 0;
    if (take(MS(3000), &p, 1) == 3) take(MS(3000), &p, 1);
    if (!motorWas) { command(0x08); if (take(MS(3000), &p, 1) == 3) take(MS(10000), &p, 1); }
    ackAll(); drain();
    ExitCriticalSection();

    printf("  region INT%d \"%s\": %s; unlock answers INT5 40h %d of 7; ReadN LBA 16 INT%d", fr, reg,
           str ? str : "no unlock for it", n5, f1);
    if (f1 == 1) printf(" %02x %c%c%c%c%c - %s\n", d[0], d[1], d[2], d[3], d[4], d[5],
                        (d[0] == 1 && d[1] == 'C' && d[2] == 'D') ? "READS" : "read, not a volume descriptor");
    else printf(" %02x %02x - REFUSED\n", s1[0], s1[1]);
    printf("Done.\n");
    return 0;
}
