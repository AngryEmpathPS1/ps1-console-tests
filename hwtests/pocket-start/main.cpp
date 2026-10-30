/*
 * sio/pocket-start - how long a PocketStation takes to act on a 59h start (2026-10-24)
 *
 * WHY. hwtests/pocket-load on a PocketStation: 5Ah straight after the 59h still read dir_index 0000 (FLAG 00), and 0001
 * (FLAG 08) by 2 s; this emulator's reads 0001 at once. psx-spx: 59h sets a request (ComFlags.11) that the GUI handles in its
 * main loop. This times the switch, both ways.
 *
 * WHAT IT DOES. Needs an application in block 1 (pocket-load leaves Doko Demo Issho's). Five times: 59h dir_index 0000 with
 * parameter 10h (back to the GUI - psx-spx: 10h or 20h keep the RTC), then 5Ah every ~20 ms for 3 s, printing each change of
 * dir_index or FLAG with its time; then 59h dir_index 0001 parameter 0 (the game's own), the same. Times in ms from the 59h's
 * last byte.
 *
 * THE RULE: run it here first: `node hwtests/run.js pocket-start --pocket2 --frames=6000` (after pocket-load on the same core).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define PORT 1

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }
static uint32_t clk;                 /* Timer 2 unwrapped, in cycles (sysclock) */
static uint16_t lastT2;
static void tick() { const uint16_t n = t2(); clk += (uint16_t)(n - lastT2); lastT2 = n; }
static int ms() { tick(); return (int)(clk / 33869); }

static void begin() {
    write16(JOY_CTRL, 0x0040); write16(JOY_MODE, 0x000D); write16(JOY_BAUD, 0x0088); write16(JOY_CTRL, 0x0000);
    for (int i = 0; i < 4; i++) { wait(5000); tick(); }
    write16(JOY_CTRL, 0x0003 | (PORT << 13));
    wait(1500); tick();
}
static void end() { write16(JOY_CTRL, 0x0000); }

static int xfer(uint8_t v, int needAck) {
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x0013 | (PORT << 13));
    write8(JOY_DATA, v);
    const uint16_t t0 = t2(); int acked = 0;
    for (;;) {
        const uint32_t s = read32(JOY_STAT);
        if (s & 0x80) acked = 1;
        if ((s & 2) && (acked || !needAck)) break;
        if ((uint16_t)(t2() - t0) > 30000) break;
    }
    tick();
    const int rx = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : -2;
    return (needAck && !acked) ? -1 : rx;
}

/* 5Ah: dir_index in *dir, the FLAG byte returned (-1 when it broke off). */
static int poll(int* dir) {
    begin();
    int flag = -1, hi = -1, lo = -1;
    if (xfer(0x81, 1) >= 0) { flag = xfer(0x5A, 1); if (flag >= 0 && xfer(0, 1) >= 0) { hi = xfer(0, 1); lo = xfer(0, 1); for (int i = 0; i < 15; i++) xfer(0, i < 14); } }
    end();
    *dir = (hi >= 0 && lo >= 0) ? (hi << 8) | lo : -1;
    return (*dir < 0) ? -1 : flag;
}

static void start(int dir, uint32_t param) {
    begin();
    const uint8_t b[9] = { 0x81, 0x59, 0, (uint8_t)(dir >> 8), (uint8_t)dir, (uint8_t)param, (uint8_t)(param >> 8), (uint8_t)(param >> 16), (uint8_t)(param >> 24) };
    for (int i = 0; i < 9; i++) if (xfer(b[i], i < 8) < 0 && i < 8) break;
    end();
}

/* Each change recorded while interrupts are off, printed afterwards. */
static int chT[10][2][13], chD[10][2][13], chF[10][2][13], chN[10][2];
static void watch(int run, int which) {
    const int t0 = ms();
    int lastDir = -2, lastFlag = -2, n = 0;
    while (ms() - t0 < 3000) {
        int dir; const int flag = poll(&dir);
        if (dir != lastDir || flag != lastFlag) {
            chT[run][which][n] = ms() - t0; chD[run][which][n] = dir; chF[run][which][n] = flag; lastDir = dir; lastFlag = flag;
            if (++n >= 13) break;
        }
        const int tw = ms(); while (ms() - tw < 20) { }
    }
    chN[run][which] = n;
}
static void report(int run, int which, const char* what) {
    printf("  %s:", what);
    for (int i = 0; i < chN[run][which]; i++) printf(" %d:%04x/%02x", chT[run][which][i], chD[run][which][i] & 0xFFFF, chF[run][which][i] & 0xFF);
    printf("\n");
}

int main() {
    printf("\nsio/pocket-start\n");
    printf("A 59h start and 5Ah polled every ~20 ms for 3 s: time(ms from the 59h):dir_index/FLAG at each change.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    lastT2 = t2(); clk = 0;
    EnterCriticalSection();
    for (int k = 0; k < 5; k++) {
        start(0x0000, 0x10);
        watch(k, 0);
        start(0x0001, 0);
        watch(k, 1);
    }
    ExitCriticalSection();
    for (int k = 0; k < 5; k++) { printf("run %d\n", k + 1); report(k, 0, "59h 0000 (GUI)"); report(k, 1, "59h 0001 (file 1)"); }
    restoreTimer(2, prevT2);
    printf("\nsio/pocket-start: Done.\n");
    for (;;) VSync(0);
}
