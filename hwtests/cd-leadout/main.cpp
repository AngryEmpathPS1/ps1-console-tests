/*
 * cdrom/cd-leadout - how far a pressed disc's lead-out runs (2026-10-05, for the SCPH-9001).
 *
 * WHY. This emulator writes 90 s of lead-out after a disc's data (src/psx.js DISC_LEADOUT, the Red Book's minimum:
 * an ASSUMPTION; M283 noted "a pressed disc's lead-out may run longer"). Console day measured two pressed discs past
 * it:
 *   - Moto Racer World Tour: every row to its lead-out + 4:30 found audio, not the end (SeekL INT5 06h,04h after
 *     8.5 s), and SeekP at +2:30 landed (GetlocP track AAh, 65:25:62);
 *   - The X-Files Disc 1: getloc's SeekL to 74:30:00 landed at 74:29:73, "mode 2", track AAh - 16:31 past its 57:59.
 * Where does a lead-out END, then? An end near the disc's outer edge would fit both and ps1-tests' author's disc
 * (which stopped between 74:00 and 74:30, focus lost); a fixed length would not.
 *
 * WHAT IT DOES. GetTD(0) gives the lead-out's start. Then SeekP to the lead-out + 0:30, 1:30, 3:00, 5:00, 7:00, 9:00,
 * 11:00, 13:00 and 15:00 - SeekP lands by the subchannel, so a data lead-out and an audio one alike - each held at
 * 74:00:00, never past it (psx-spx 19h,15h: no outer limit switch). Each row from an Init and a SeekP 00:02:00, as
 * cd-pastend's rows: the second response and its time, then GetlocP - track (AAh in the lead-out), index, relative
 * and absolute time where the head landed. Past the written area a row answers INT5 (06h,10h where it reflects,
 * 04h,10h where focus is lost).
 *
 * READ IT LIKE THIS. The last row that lands (INT2, GetlocP track AAh) bounds the lead-out from below, the first that
 * fails from above. With The X-Files (lead-out at 57:59) all nine rows reach 72:59; with Moto Racer World Tour (62:56;
 * `--only=cd-leadout` after the disc swap) the rows past +11:04 are held at 74:00.
 *
 * SAFE FOR THE DRIVE: nothing past 74:00:00; about 1.5 minutes. Run once, at the end (tools/consolerun.js CD_END).
 * THE RULE: run here first. ON THIS CORE AT v0.366 (`node hwtests/run.js cd-leadout --disc --frames=12000
 * --quiet=2000`, The X-Files): the model lands +0:30 and +1:30 (INT2 after ~1.0 s, GetlocP AAh 01, 58:28:73 and
 * 59:28:73) and fails every row from +3:00 with INT5 04h,10h after 0.650 s - its 90 s lead-out. The SCPH-9001's own
 * program (PSX_CD_FIRMWARE=SC430944) lands the same two (1.29 s, 1.32 s) and fails the rest 06h,10h, after ~5.2 s to
 * +9:00 and ~10.5 s beyond, GetlocP INT5 03h,80h.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 0x1F; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
static uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

/* The unwrapped clock: Timer2 counts sysclock/8; call tick() often (cd-pastend's). */
static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

static uint8_t r[16];
static int n;
/* The next interrupt (up to `limit` cycles): its number, 0 if none came, its response bytes in r[] (n), acknowledged. */
static uint8_t next(uint32_t limit) {
    uint32_t t0 = now();
    uint8_t f = 0;
    while (!(f = irqFlag() & 7)) if (now() - t0 > limit) return 0;
    n = 0;
    while (hasResp() && n < 16) r[n++] = resp();
    ackAll();
    return f;
}
static const uint32_t SEC = 33868800;
static void simple(uint8_t cmd) { command(cmd); next(SEC); }
static void init() { command(0x0A); next(SEC); next(5 * SEC); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}

int main() {
    printf("\ncdrom/cd-leadout\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    init();
    simple(0x0C);                                   /* Demute */
    command(0x13); next(SEC);                       /* GetTN */
    uint8_t lastTrack = n >= 3 ? unbcd(r[2]) : 1;
    param(0); command(0x14); next(SEC);             /* GetTD(0): the lead-out */
    uint32_t total = n >= 3 ? (unbcd(r[1]) * 60 + unbcd(r[2])) * 75 : 0;
    printf("disc: %d tracks, lead-out at %d:%02d (%d sectors)\n", lastTrack, total / 4500, (total / 75) % 60, total);

    const uint32_t EDGE = 74 * 4500 - 150;          /* 74:00:00, never past it */
    static const uint16_t AFTER[9] = { 30, 90, 180, 300, 420, 540, 660, 780, 900 };   /* seconds past the lead-out */
    uint32_t prevT = 0;
    for (int k = 0; k < 9; k++) {
        uint32_t t = total - 150 + AFTER[k] * 75;
        if (t > EDGE) t = EDGE;
        if (k && t == prevT) { printf("SeekP lead-out+%d:%02d: held at 74:00:00, as the row before\n", AFTER[k] / 60, AFTER[k] % 60); continue; }
        prevT = t;
        init();
        param(0x00); simple(0x0E);
        setloc(0); command(0x16); next(SEC); next(5 * SEC);   /* SeekP 00:02:00 */
        setloc(t);
        const uint32_t a = t + 150;
        printf("SeekP lead-out+%d:%02d %02d:%02d:%02d:", AFTER[k] / 60, AFTER[k] % 60, a / 4500, (a / 75) % 60, a % 75);
        command(0x16);
        uint8_t f = next(SEC);
        const uint32_t t0 = now();
        printf(" INT%d %02x,", f, n ? r[0] : 0);
        f = next(20 * SEC);
        const uint32_t dt = now() - t0;
        if (!f) { printf(" (none in 20 s)\n"); continue; }
        printf(" then INT%d", f);
        for (int i = 0; i < n; i++) printf(" %02x", r[i]);
        printf(" after %d.%03d s", dt / SEC, (dt % SEC) / (SEC / 1000));
        command(0x11);                              /* GetlocP */
        f = next(SEC);
        printf("; GetlocP INT%d", f);
        for (int i = 0; i < n; i++) printf(" %02x", r[i]);
        printf("\n");
    }

    init();
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
