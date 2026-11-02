/*
 * gpu/gpu-gp1scan - does any undocumented GP1 command move a flat rectangle's rows? (2026-11-01)
 *
 * WHY. The first SCPH-5501's boot of 2026-11-01 pays 2 cycles at each row of a write that runs flat out, against every log before it (hwtests/gpu-wh). No readable
 * register differs and GP1(00h) leaves it. If a state does this, a command may reach it. psx-spx has GP1(09h), GP1(20h) and little else above 08h: the rest
 * is either ignored or unmeasured.
 *
 * WHAT IT DOES. Interrupts off, no disc, display off, Timer 2 at the system clock. A flat 8x128 rectangle timed, words to GPUSTAT bit 26 by a poll, fastest of three:
 * 789 is a normal console's in hwtests/gpu-shape-time (D, a shorter measure than this one), 1,045 the slow boot's, so 256 cycles apart. Then, for GP1 commands
 * 09h..0Fh and 20h..3Fh, each with arguments 0, 1, 2, 3, 4, 8, 10h, 501h, 504h and FFFFFFh: the command, the rectangle again, and GPUSTAT. GP1(00h) and
 * GP1(03h) between commands, so each starts where the documented state is.
 *
 * NOT GP1(0Bh) AT 274h OR MORE. psx-spx: "GPU crashes after a while when set to 274h..7FFh", and its first version here, sent 501h, did: GPUSTAT 00802000h, never
 * ready again, GP1(00h) does not bring it back, only RESET; the CPU and the runner carried on. So 0Bh gets its own sweep below that, 0..272h by steps, and
 * the command is put back to 0 behind each.
 *
 * READ IT LIKE THIS. B base. G cmd arg t stat: every row within 40 of base is left out and counted. E the base again at the end. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-gp1scan --frames=3000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static int timeouts;
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void ready() { for (int i = 0; i < 200000; i++) if (read32(GP1) & 0x04000000u) return; timeouts++; write32(GP1, 0x01000000u); }

__attribute__((noinline)) static uint32_t once() {
    ready();
    waitT2(600);
    const uint16_t t0 = read16(T2_COUNT);
    write32(GP0, 0x60408020u);
    write32(GP0, (128u << 16) | 192u);
    write32(GP0, (128u << 16) | 8u);
    for (int i = 0; i < 40000; i++) if (read32(GP1) & 0x04000000u) break;
    return (uint16_t)(read16(T2_COUNT) - t0);
}
static uint32_t rect() { uint32_t m = 0xFFFF; for (int i = 0; i < 3; i++) { const uint32_t t = once(); if (t < m) m = t; } return m; }
static void env() {
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
}

int main() {
    static const uint32_t args[10] = { 0, 1, 2, 3, 4, 8, 0x10, 0x501, 0x504, 0xFFFFFF };
    static uint32_t out[400][4];
    int n = 0, same = 0;
    printf("\ngpu/gpu-gp1scan\n");
    printf("B base; G cmd arg t stat, rows within 40 of base left out and counted; E the base at the end. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    env();
    const uint32_t base = rect();
    for (int v = 0; v <= 0x272; v += 0x11) {
        env();
        write32(GP1, 0x0B000000u | (uint32_t)v);
        waitT2(400);
        const uint32_t t = rect(), st = read32(GP1);
        const int d = (int)t - (int)base;
        if (d > 40 || d < -40) { if (n < 400) { out[n][0] = 0x0B; out[n][1] = v; out[n][2] = t; out[n][3] = st; n++; } }
        else same++;
        write32(GP1, 0x0B000000u);
    }
    for (int c = 0x09; c <= 0x3F; c++) {
        if ((c >= 0x10 && c <= 0x1F) || c == 0x0B) continue;
        for (int a = 0; a < 10; a++) {
            env();
            write32(GP1, ((uint32_t)c << 24) | args[a]);
            waitT2(400);
            const uint32_t t = rect(), st = read32(GP1);
            const int d = (int)t - (int)base;
            if (d > 40 || d < -40) { if (n < 400) { out[n][0] = c; out[n][1] = args[a]; out[n][2] = t; out[n][3] = st; n++; } }
            else same++;
        }
    }
    env();
    const uint32_t end = rect();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("B %u\n", base);
    for (int i = 0; i < n; i++) printf("G %x %x %u %x\n", out[i][0], out[i][1], out[i][2], out[i][3]);
    printf("same %d\nE %u\n\n%d timeouts\ngpu/gpu-gp1scan: Done.\n", same, end, timeouts);
    for (;;) VSync(0);
}
