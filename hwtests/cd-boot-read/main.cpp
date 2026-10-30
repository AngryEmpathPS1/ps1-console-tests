/*
 * cdrom/cd-boot-read - the BIOS's first double-speed read, as the BIOS makes it (2026-10-19)
 *
 * WHY. Three boots of The X-Files recorded off the SCPH-7501 through the capture card (tools/capturefields.js --arm) hold
 * the SONY screen 0.1-0.44 s longer than this emulator (its fade-out 11.61 / 11.91 / 11.95 s after the console's first
 * signal, here 11.51); the 7501's own drive program (SC430940) on the servo lands inside the console's spread (11.86).
 * tools/cdtrace.js of the boot on both: the BIOS's 18 CD commands alike from frame 498, and the step that differs is its
 * first double-speed read - Setloc 00:02:04, SeekL, Setmode 80h at the SeekL's INT2, ReadN at the Setmode's INT3 - whose
 * first INT1 comes 26.62M after the ReadN on this emulator's model and 41.07M on the drive's program (its spindle doubles
 * in ~35 frames while the head runs on up the spiral, then walks back by single tracks to the target, four sectors into
 * the program area). hwtests/cd-speed times the speed change at 00:06:16 (READ, 50 ms between the Setmode and the
 * ReadN): the console 25.7M-26.5M, the model 26M, the program 21.5M - so the program's 41M is not the change itself but
 * where the BIOS reads. This asks a console the BIOS's own step at the BIOS's own place, beside two more.
 *
 * WHAT IT DOES. Interrupts off, every wait bounded, a first flag of 1 or 4 read again (CLAUDE.md rule 1), Timer 2 at the
 * system clock, unwrapped. For each place - 00:02:04 (the BIOS's), 00:02:16, 00:06:16 (cd-speed's) - three times: single
 * speed settled 1.5 s, Setloc, SeekL to its INT2; then at once Setmode 80h, and at its INT3 ReadN: the first INT1's time
 * from the ReadN's write, and GetlocL at once - the sector that came first (its header's MSF); then Pause to its INT2.
 * The CONTROL: the same with Setmode 00h (no speed change). Nothing prints until the end (the drive paused).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-boot-read --disc=xfiles --frames=12000 --quiet=10000`.
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

struct Got { uint8_t flag, stat; uint32_t t; uint8_t r[8]; };
/* The next flag (up to `limit` cycles): when, its first eight response bytes, drained and acknowledged. */
static Got take(uint32_t limit = MS(3000)) {
    Got g = { 0, 0, 0, { 0 } };
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }   /* half of 3 or 5? (rule 1) */
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    for (int i = 0; i < 8 && hasResp(); i++) g.r[i] = resp();
    g.stat = g.r[0];
    drain();
    ackAll();
    return g;
}
/* The next INT of kind `want` (an INT5 ends the wait too), acknowledging anything else on the way. */
static Got takeKind(uint8_t kind, uint32_t limit) {
    const uint32_t t0 = now();
    for (;;) {
        const Got g = take(limit);
        if (!g.flag || g.flag == kind || g.flag == 5) return g;
        if (now() - t0 > limit) { Got z = { 0, 0, now(), { 0 } }; return z; }
    }
}
static void setmode(uint8_t m) { ackAll(); param(m); command(0x0E); take(); }
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); command(0x02); take();
}
static void pauseDrive() { ackAll(); command(0x09); if (take().flag == 3) takeKind(2, MS(3000)); }

struct Row { uint32_t seekInt2, readFirst; uint8_t flag, m, s, f; };
static const uint32_t PLACES[3] = { 4, 16, 316 };       /* 00:02:04 (the BIOS's), 00:02:16, 00:06:16 (cd-speed's) */
static Row rows[2][3][3];

static void one(Row &row, uint32_t lba, uint8_t mode2) {
    setmode(0x00); idle(MS(1500));                       /* single speed, settled */
    setloc(lba);
    ackAll(); const uint32_t tS = now(); command(0x15);  /* SeekL */
    take(); const Got s2 = takeKind(2, MS(4000));
    row.seekInt2 = s2.flag == 2 ? s2.t - tS : 0;
    ackAll(); param(mode2); command(0x0E);               /* Setmode at once, as the BIOS */
    take();
    ackAll(); const uint32_t tR = now(); command(0x06);  /* ReadN at its INT3 */
    take();
    const Got g = takeKind(1, MS(4000));
    row.flag = g.flag; row.readFirst = g.flag ? g.t - tR : 0;
    row.m = row.s = row.f = 0;
    if (g.flag == 1) {                                   /* GetlocL at once: the sector that came first */
        ackAll(); command(0x10);
        const Got l = takeKind(3, MS(500));
        if (l.flag == 3) { row.m = l.r[0]; row.s = l.r[1]; row.f = l.r[2]; }
    }
    pauseDrive();
}

int main() {
    printf("\ncdrom/cd-boot-read\n");
    printf("The BIOS's first double-speed read: SeekL from single speed, Setmode 80h at its INT2, ReadN at the Setmode's\n"
           "INT3 - the first INT1 from the ReadN's write, and GetlocL then (the first sector's header). Cycles at the system\n"
           "clock (1 ms = 33,869). About a minute; nothing prints until the end.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0); now();
    ackAll(); drain();
    ackAll(); command(0x0A); if (take().flag == 3) takeKind(2, MS(5000));   /* Init: the drive spun up, single speed */
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 3; p++)
            for (int r = 0; r < 3; r++) one(rows[k][p][r], PLACES[p], k ? 0x00 : 0x80);
    setmode(0x00);
    ExitCriticalSection();
    for (int k = 0; k < 2; k++) {
        printf("%s\n", k ? "CONTROL, Setmode 00h (no speed change):" : "TO DOUBLE SPEED, Setmode 80h:");
        for (int p = 0; p < 3; p++) {
            const uint32_t a = PLACES[p] + 150;
            printf("  %02d:%02d:%02d :", (int)(a / 4500), (int)((a / 75) % 60), (int)(a % 75));
            for (int r = 0; r < 3; r++) {
                const Row &w = rows[k][p][r];
                printf("  seek %8d  read INT%d %9d  first %02x:%02x:%02x", (int)w.seekInt2, w.flag, (int)w.readFirst, w.m, w.s, w.f);
            }
            printf("\n");
        }
    }
    printf("Done.\n");
    for (;;) {}                                          /* park: Unirom's resident handler stays reachable */
    return 0;
}
