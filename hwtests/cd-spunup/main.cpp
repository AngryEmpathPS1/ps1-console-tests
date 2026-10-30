/*
 * cdrom/cd-spunup — how a command's first response moves in the seconds after the disc spins up.
 *
 * WHY THIS TEST EXISTS. hwtests/cd-init's HELD section (nugget's cdlInitDelayed) sends three Inits, each held 500 ms
 * before its acknowledge. On console day the SECOND of them answered at 104-184k cycles on five of six runs (SCPH-5501
 * 183,833 and 145,451; 7501 122,923 and 122,912; 9001 104,159 - its other run 63,840), where every other Init from a
 * spinning disc answers at 60-112k (BACK, SETTLED) - and the third, sent the same way, at 70k again. What the three
 * share is the hold; what sets the second apart is when it is sent. The section before HELD ends with an Init that
 * spins the disc up (a Stop mid-read, 500 ms, Init), so HELD's first Init is written at its INT2 and the second ~0.55 s
 * later, the third ~1.1 s later: the drive may still be finishing its start-up - the focus and tracking settling, the
 * servo's own adjustments - for the first second or so, and answer late meanwhile. Nothing here models that: v0.377.36
 * answers the second as it answers BACK.
 *
 * WHAT IT DOES (any data disc; ~75 s, the motor stopped and started 16 times - one of the heavy CD tests).
 *   SPUN UP: Stop (and its INT2), 2 s, Init from the stopped disc (and its INT2: the spin-up), then D ms idle and an
 *     Init, for D = 0, 100, 200, 300, 400, 500, 600, 800, 1000, 1500 - its INT3 from the write, its INT2 from the INT3.
 *   GETSTAT: the same with Getstat at D = 0, 250, 550, 1000 - does every command answer late then, or Init alone?
 *   HELD AGAIN: cd-init's HELD as it ran, twice: a spin-up Init, then three Inits each held 500 ms - the three INT3s.
 *   CONTROL: the same delays after an ordinary Init (the disc already spinning): Init INT3 at D = 0, 500, 1000.
 * All four with the motor at single speed. No assertions: the rows are the answer.
 *
 * WHAT DECIDES IT. If the SPUN UP rows are late up to some D and BACK-like after it, the drive's start-up runs that
 * long after the INT2, and the model gives Init (and Getstat, if it moves) that window. If they are never late and HELD
 * AGAIN is, the hold itself is the cause. CONTROL must read like cd-init's BACK and SETTLED, or the run is suspect.
 *
 * ON THIS CORE (v0.377.36 as the SCPH-7501, The X-Files Disc 1, the SCPH-7001 BIOS: `node hwtests/run.js cd-spunup
 * --disc=xfiles --frames=12000 --quiet=8000 --ntsc --settle=600`): nothing late after a spin-up - SPUN UP's Inits 60.7k-
 * 93.9k at every D (BACK's and SETTLED's tables), their INT2s 0.37M-4.24M (where the circling head is); Getstat 28.5k-
 * 96.3k; HELD AGAIN's Inits 63.9k-89.2k, the second of each 77.7k and 71.3k; CONTROL 61.3k-111.0k. The spin-up Inits
 * 90.6k-104.5k, their INT2 63.0M (the 7501's spin-up).
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
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */

/* Wrap-safe: every loop that waits calls now(), far more often than once per 65,536 cycles. */
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

struct Got { uint8_t flag, stat; uint32_t t; };
/* Wait for any flag (up to `limit` cycles), note when, read its first response byte, drain, acknowledge. */
static Got take(uint32_t limit = MS(3000)) {
    Got g = { 0, 0, 0 };
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        const uint8_t f = irqFlag();
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    if (hasResp()) g.stat = resp();
    drain();
    ackAll();
    return g;
}

struct Row { Got a, b; uint32_t t0; };
/* A command: its INT3 from the write, and (with `second`) its INT2 from the INT3. */
static Row cmd(uint8_t c, bool second) {
    Row r;
    ackAll();
    r.t0 = now();
    command(c);
    r.a = take();
    r.b = second && r.a.flag == 3 ? take(MS(6000)) : Got{ 0, 0, 0 };
    return r;
}
/* Stop, 2 s, and an Init that spins the disc up, each to its INT2. */
static Row spinUp() {
    cmd(0x08, true);
    idle(MS(2000));
    return cmd(0x0A, true);
}

static const uint16_t D_INIT[10] = { 0, 100, 200, 300, 400, 500, 600, 800, 1000, 1500 };
static const uint16_t D_STAT[4] = { 0, 250, 550, 1000 };
static const uint16_t D_CTRL[3] = { 0, 500, 1000 };
static Row spun[10], spunUp[10], stat[4], ctrl[3];
static uint32_t heldAck[2][3], heldAfter[2][3];

static void show(const char *name, unsigned d, const Row &r) {
    printf("  %-8s D %4u ms  INT%d %8u (%02x)", name, d, r.a.flag, (unsigned)(r.a.t - r.t0), r.a.stat);
    if (r.b.flag) printf("   INT%d %9u after it (%02x)\n", r.b.flag, (unsigned)(r.b.t - r.a.t), r.b.stat);
    else printf("\n");
}

int main() {
    printf("\ncdrom/cd-spunup\n");
    printf("A command's first response in the seconds after the disc spins up. Needs a disc. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();

    cmd(0x0A, true);                                                 /* a known state: spinning, single speed */
    for (int i = 0; i < 10; i++) {
        spunUp[i] = spinUp();
        idle(MS(D_INIT[i]));
        spun[i] = cmd(0x0A, true);
    }
    for (int i = 0; i < 4; i++) {
        spinUp();
        idle(MS(D_STAT[i]));
        stat[i] = cmd(0x01, false);                                  /* Getstat */
    }
    for (int r = 0; r < 2; r++) {
        spinUp();
        for (int i = 0; i < 3; i++) {                                /* cd-init's HELD, as it ran */
            ackAll();
            const uint32_t t0 = now();
            command(0x0A);
            uint8_t f;
            do { f = irqFlag(); } while (!f && now() - t0 < MS(3000));
            heldAck[r][i] = now() - t0;
            idle(MS(500));
            drain();
            ackAll();
            const uint32_t a0 = now();
            const Got g = take();
            heldAfter[r][i] = g.flag == 2 ? g.t - a0 : 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        cmd(0x0A, true);                                             /* an ordinary Init, the disc spinning */
        idle(MS(D_CTRL[i]));
        ctrl[i] = cmd(0x0A, true);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("Cycles at the system clock (1 ms = 33,869). INT3 from the command write, INT2 from the INT3.\n");
    printf("SPUN UP (Stop, 2 s, Init from the stopped disc, D ms, Init):\n");
    for (int i = 0; i < 10; i++) { show("spin-up", 0, spunUp[i]); show("Init", D_INIT[i], spun[i]); }
    printf("GETSTAT (the same, then Getstat):\n");
    for (int i = 0; i < 4; i++) show("Getstat", D_STAT[i], stat[i]);
    printf("HELD AGAIN (a spin-up Init, then three Inits each held 500 ms - cd-init's HELD):\n");
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 3; i++)
            printf("  Init %d    INT3 %8u   INT2 %u after the acknowledge\n", i + 1, (unsigned)heldAck[r][i], (unsigned)heldAfter[r][i]);
    printf("CONTROL (an Init with the disc spinning, D ms, Init):\n");
    for (int i = 0; i < 3; i++) show("Init", D_CTRL[i], ctrl[i]);
    printf("\ncdrom/cd-spunup: Done.\n");
    for (;;) VSync(0);
}
