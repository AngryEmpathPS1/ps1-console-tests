/*
 * cdrom/cd-spinprobe - what a drive that will not read says, step by step: Getstat every 50 ms across Init, MotorOn and ReadTOC
 * (2026-11-09).
 *
 * WHY. The PSone PM-41(2) (SC430949) on the bridge's port 3: its first run's disc check found no disc - after Init, Getstat 00
 * (motor off) for 15 s and GetTN INT5 01 80, 21 times (hwtests/cd-discid's discid-wait line) - and already at the run's start,
 * before any test, its Stop came back INT5 01 where every other console's is INT3 then INT2. The author: The X-Files is in it, the
 * disc spins at power-on - even with the door open (its lid switch reads closed whatever the lid). So: does the motor come on to
 * any command, does the drive try to focus and give up, and with which error codes?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; every wait bounded (rule 1). In order: Getstat; Test 20h (the
 * version); Init (its first and second response); Getstat every 50 ms for 8 s, each CHANGE kept; MotorOn (07h) and its
 * responses; 8 s of Getstat changes; ReadTOC (1Eh) and its responses (the second up to 20 s); 8 s of Getstat changes; GetTN.
 * Each response: its INT number and every byte. Printed after interrupts return. No seek, no read of a sector. About 40 s.
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
/* the next response within `limit` (its INT number, 0 if none; rb its bytes), psx-spx's polling: a 1 or 4 read again */
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

/* the log, printed after interrupts return */
#define MAXL 160
static struct { uint32_t ms; char what; uint8_t intn, n, b[8]; } lg[MAXL];
static int nl;
static uint32_t base;
static void note(char what, uint8_t intn) {
    if (nl >= MAXL) return;
    lg[nl].ms = (now() - base) / 33869u; lg[nl].what = what; lg[nl].intn = intn; lg[nl].n = (uint8_t)nrb;
    for (int i = 0; i < 8; i++) lg[nl].b[i] = i < nrb ? rb[i] : 0;
    nl++;
}
/* a command, its first response, and (second) the next within `second2` */
static uint8_t cmdLog(char tag, uint8_t c, const uint8_t *p, int np, uint32_t second2) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    const uint8_t f = take(MS(3000)); note(tag, f);
    if (second2 && f == 3) { const uint8_t g = take(second2); note((char)(tag + 32), g); }   /* lower case: the second */
    return f;
}
/* Getstat every 50 ms for `ms`, a line for each status that differs from the one before */
static void watch(uint32_t ms) {
    int last = -1; const uint32_t t0 = now();
    while (now() - t0 < MS(ms)) {
        command(0x01);
        const uint8_t f = take(MS(1000));
        const int st = (f == 3 && nrb) ? rb[0] : 0x100 + f;
        if (st != last) { note('S', f); last = st; }
        idle(MS(50));
    }
}

int main() {
    printf("\ncdrom/cd-spinprobe\nThe drive's status every 50 ms across Init, MotorOn and ReadTOC; every response byte.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now(); base = now();
    ackAll(); drain();
    const uint8_t v[1] = { 0x20 };
    cmdLog('G', 0x01, 0, 0, 0);                 /* Getstat */
    cmdLog('V', 0x19, v, 1, 0);                 /* Test 20h: the version */
    cmdLog('I', 0x0A, 0, 0, MS(10000));         /* Init, its second response */
    watch(8000);
    cmdLog('M', 0x07, 0, 0, MS(10000));         /* MotorOn, its second response */
    watch(8000);
    cmdLog('R', 0x1E, 0, 0, MS(20000));         /* ReadTOC, its second response */
    watch(8000);
    cmdLog('T', 0x13, 0, 0, 0);                 /* GetTN */
    ExitCriticalSection();
    for (int i = 0; i < nl; i++) {
        const char w = lg[i].what;
        const char *name = w == 'G' ? "Getstat" : w == 'V' ? "Test 20h" : w == 'I' ? "Init" : w == 'i' ? "Init, second" : w == 'M' ? "MotorOn"
                         : w == 'm' ? "MotorOn, second" : w == 'R' ? "ReadTOC" : w == 'r' ? "ReadTOC, second" : w == 'T' ? "GetTN" : "status";
        printf("  %6lu ms  %-16s INT%u", (unsigned long)lg[i].ms, name, lg[i].intn);
        for (int k = 0; k < lg[i].n && k < 8; k++) printf(" %02X", lg[i].b[k]);
        printf("\n");
    }
    printf("cdrom/cd-spinprobe: Done.\n");
    return 0;
}
