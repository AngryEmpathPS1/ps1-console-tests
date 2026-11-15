/*
 * cdrom/cd-motor - IS THE DISC SPINNING? (2026-11-15). Not
 * a test: the rack page (tools/rackpage.js) runs it through the console queue to show each console's disc as spinning or stopped.
 * Getstat (01h) only - never Init (cd-discid's first command), which would spin the disc up to answer. Twice: psx-spx "Getstat"
 * - the shell-open bit (4) latches until a Getstat with the lid closed reads it, so the second answer is the lid as it is now.
 * Stat bits (psx-spx "CDROM Controller Status"): 1 motor on, 4 shell open, 5 reading, 6 seeking, 7 playing.
 * Prints "motor: stat SS (first SS) spinning|stopped lid open|closed" and "Done.". Interrupts off; every wait bounded.
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
/* A command to its first response (its flag; rb holds the bytes), then with `second` its second response's flag in *f2. */
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second, uint8_t *f2) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t f; const uint32_t t0 = now();
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    if (second) { uint8_t g = 0; if (f == 3) { do { g = take(MS(10000)); } while (g == 1 && now() - t0 < MS(10000)); } if (f2) *f2 = g; }
    return f;
}
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }


int main() {
    printf("\ncdrom/cd-motor\nIs the disc spinning: Getstat twice, nothing else.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    const uint8_t f1 = cmd(0x01, 0, 0, false, 0); const uint8_t s1 = nrb ? rb[0] : 0xFF;
    idle(MS(20));
    const uint8_t f2 = cmd(0x01, 0, 0, false, 0); const uint8_t s2 = nrb ? rb[0] : 0xFF;
    ExitCriticalSection();
    if (f2 != 3) printf("motor: no answer (Getstat INT%u, then INT%u)\n", f1, f2);
    else printf("motor: stat %02X (first %02X) %s lid %s\n", s2, s1, (s2 & 0x02) ? "spinning" : "stopped", (s2 & 0x10) ? "open" : "closed");
    printf("cdrom/cd-motor: Done.\n");
    return 0;
}
