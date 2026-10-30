/*
 * gpu/gp0-poll - gpu-cost-sweep's own write-then-poll sequence, every poll recorded (2026-10-14 small hours).
 *
 * WHY. gpu-cost-sweep and gpu-thin write a primitive's words with a C loop of write32() and then poll GPUSTAT, taking the
 * time each of bits 28 and 26 is first seen. On the SCPH-7501 a 1x1 rectangle's bits read 140 / 140 cycles from its first
 * word where this core reads 91 / 141 - every 3-word command's bit 28 ~50 late there - while the exact method (a single
 * read D cycles after back-to-back stores) and gp0-store (the CPU behind GP0 stores: ~1-2 cycles a store, a GPUSTAT read
 * behind a command's words 5-8 longer than here) do not show it. Does the console's first poll come late, or come on
 * time and see bit 28 clear?
 *
 * WHAT IT DOES. gpu-cost-sweep's drawOnce() exactly (wait for bits 26 and 28, t0 = Timer 2, the words by write32() in a
 * loop, then GPUSTAT and Timer 2 read in turn) but every one of the first six polls kept: its time from t0 and its bits
 * 28 / 26. Four runs of each: a 1x1 rectangle (60h, 3 words), a 16x16 rectangle, a 16x16 fill (02h), a level line of 16
 * (40h, 3 words), a flat legs-4 triangle (20h, 4 words), and two GP0(00h) NOPs then a GPUSTAT poll (2 words, no command).
 *
 * READ IT LIKE THIS. Per run six `t:bb` pairs, b the bits 28 and 26. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gp0-poll --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

static void polls(const char* name, const uint32_t* w, int n) {
    printf("%-14s", name);
    for (int run = 0; run < 4; run++) {
        uint32_t ts[6], ss[6];
        waitIdle();
        const uint32_t t0 = now();
        for (int i = 0; i < n; i++) write32(GP0, w[i]);
        for (int k = 0; k < 6; k++) { const uint32_t s = read32(GP1), t = now() - t0; ts[k] = t; ss[k] = s; }
        if (run == 0) continue;   /* the first warms the code */
        printf(" |");
        for (int k = 0; k < 6; k++) printf(" %u:%u%u", (unsigned)ts[k], (unsigned)((ss[k] >> 28) & 1), (unsigned)((ss[k] >> 26) & 1));
    }
    printf("\n");
}

int main() {
    printf("\ngpu/gp0-poll\n");
    printf("gpu-cost-sweep's write-then-poll, the first six polls of three runs: t:bb = cycles from the first word, bits 28 26.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u);
    waitIdle();
    const uint32_t r1[] = { 0x60404040u, xy(0, 300), xy(1, 1) };
    const uint32_t r16[] = { 0x60404040u, xy(0, 300), xy(16, 16) };
    const uint32_t f16[] = { 0x02404040u, xy(0, 320), xy(16, 16) };
    const uint32_t l16[] = { 0x40404040u, xy(0, 340), xy(15, 340) };
    const uint32_t t4[] = { 0x20404040u, xy(0, 350), xy(4, 350), xy(0, 354) };
    const uint32_t n2[] = { 0, 0 };
    polls("rect 1x1", r1, 3); polls("rect 16x16", r16, 3); polls("fill 16x16", f16, 3);
    polls("line 16", l16, 3); polls("tri flat 4", t4, 4); polls("2 nops", n2, 2);
    waitIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gp0-poll: Done.\n");
    for (;;) {}
}
