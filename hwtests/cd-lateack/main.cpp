/*
 * cdrom/cd-lateack - a command's second response held behind its INT3 the host acknowledges late: how long after the acknowledge
 * does the INT2 come? (the Fable CD session, 2026-11-25)
 *
 * WHY. nugget's cdlInitDelayed (tests/cdrom/cdlinit.c) acknowledges an Init's INT3 500 ms late and asserts its INT2 within 1,000 us
 * of the acknowledge; its author's SCPH-9001 "typically takes 350us". The model, the drive's own program (SC430944 as the 9001)
 * on our body, takes 0.83-1.28 ms over the refresh phases (KNOWN-OPEN beside the acknowledge in src/cddrive.js; experiments/
 * nugdrift/README.md) - as if the program delivered the held response from its loop at its tick's pace. None of our consoles has
 * timed it, and nothing else has timed a held second response for any other command.
 *
 * WHAT IT DOES. Needs a disc (any data disc). Interrupts off, the CD's flags polled; Timer 2 at the system clock unwrapped in
 * software. Init and Demute first (a spun-up, paused drive), then for each command - Init (0Ah), GetID (1Ah), Pause (09h, the drive
 * already paused) - and each acknowledge delay A of 0, 50, 200, 500 ms, six trials: write the command, take its INT3 (flag seen,
 * response read), wait A, acknowledge, then time the first flag after the acknowledge (cycles) and read it (2, or 5 on an error).
 * With A = 0 that is the command's ordinary INT3 -> INT2 time; past it, the held response's delivery. A trial whose INT2 came
 * before the acknowledge cannot (the drive holds it), but a flag already set at the acknowledge would show as a time near 0.
 * Printed per (command, A): the six times in cycles and their flags. Every wait bounded; the flag read again on a first 1 or 4
 * (rule 1). No assertions - compare against your emulator. Prints "Done.". About 40 s.
 *
 * Run here first: `node hwtests/run.js cd-lateack --disc=xfiles --frames=4000 --quiet=3500 --settle=600` (PSX_MODEL and
 * PSX_CD_FIRMWARE the console's).
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
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next flag within `limit` cycles (0 on none), seen at *at; read again on a first 1 or 4 (rule 1). Not acknowledged. */
static uint8_t waitFlag(uint32_t limit, uint32_t *at) {
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag();
        if (f) { *at = now(); if (f == 1 || f == 4) { idle(200); f = irqFlag(); } return f; }
        if (now() - t0 > limit) return 0;
    }
}
/* A command run to its end, acknowledged at once (setup only). */
static void plain(uint8_t c) {
    uint32_t at; command(c);
    uint8_t f = waitFlag(MS(3000), &at); drain(); ackAll();
    if (f == 3 && (c == 0x0A || c == 0x1A || c == 0x09)) { f = waitFlag(MS(10000), &at); drain(); ackAll(); }
}

#define N_T 6
static const uint8_t CMDS[3] = { 0x0A, 0x1A, 0x09 };
static const char *NAMES[3] = { "Init ", "GetID", "Pause" };
static const uint16_t DELAYS[4] = { 0, 50, 200, 500 };
static uint32_t res[3][4][N_T]; static uint8_t flg[3][4][N_T], first[3][4][N_T];

int main() {
    printf("\ncdrom/cd-lateack\nA second response held behind an INT3 acknowledged A ms late: cycles from the acknowledge to the next flag (1 ms = 33,869).\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    plain(0x0A);                                          /* Init */
    plain(0x0C);                                          /* Demute */
    for (int c = 0; c < 3; c++) for (int a = 0; a < 4; a++) for (int k = 0; k < N_T; k++) {
        uint32_t at = 0;
        command(CMDS[c]);
        first[c][a][k] = waitFlag(MS(3000), &at);          /* its INT3 */
        drain();
        idle(MS(DELAYS[a]));
        const uint8_t pre = irqFlag();                     /* still the INT3 (3) at the acknowledge */
        ackAll();
        const uint32_t t0 = now();
        uint32_t at2 = t0;
        const uint8_t f = waitFlag(MS(10000), &at2);
        res[c][a][k] = f ? at2 - t0 : 0xFFFFFFFFu;
        flg[c][a][k] = (uint8_t)(f | (pre << 4));
        drain(); ackAll();
        idle(MS(30));
    }
    ExitCriticalSection();
    for (int c = 0; c < 3; c++) for (int a = 0; a < 4; a++) {
        printf("%s A=%3d ms:", NAMES[c], DELAYS[a]);
        for (int k = 0; k < N_T; k++) printf(" %u(%x%x/%x)", (unsigned)res[c][a][k], flg[c][a][k] >> 4, flg[c][a][k] & 15, first[c][a][k]);
        printf("\n");
    }
    printf("(each: cycles(flag at the acknowledge, flag after it / the command's first flag))\n");
    printf("cdrom/cd-lateack: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
