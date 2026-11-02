/*
 * gpu/gpu-revive - can a GPU stopped by GP1(0Bh) be brought back from software? (2026-11-02)
 *
 * WHY. hwtests/gpu-gp1scan's first version sent GP1(0Bh) 501h, 504h and FFFFFFh and left the last in place: GPUSTAT 00802000h, then 80042000h, never ready, on a console nobody
 * is at. Each GP1(00h) of that scan ran with the crash value still set. psx-spx calls the range "crashes after a while", which reads as a timer.
 *
 * WHAT IT DOES. Reads GPUSTAT, then in turn, with 200,000 cycles after each: GP1(0Bh) 0; GP1(00h); GP1(01h); GP1(02h); all four together; GP1(0Bh) 273h, the top of the safe range, and
 * GP1(00h); GP1(0Bh) 0 and GP1(00h) again; then a long wait of 8 million cycles and GP1(00h). After each, GPUSTAT, and whether a flat 8x8 goes in and finishes.
 *
 * READ IT LIKE THIS. S step stat ok t: ok 1 if bit 26 reads set and the rectangle's words went in and it came back ready, t its time on Timer 2. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-revive --frames=1200 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static void waitT2(uint32_t n) { while (n) { const uint32_t c = n > 30000 ? 30000 : n; const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < c) {} n -= c; } }
static uint32_t st[10], tm[10], ok[10];
static void check(int i) {
    waitT2(200000);
    st[i] = read32(GP1); ok[i] = 0; tm[i] = 0;
    if (!(st[i] & 0x04000000u)) return;
    const uint16_t t0 = read16(T2_COUNT);
    write32(GP0, 0x60408020u);
    write32(GP0, (128u << 16) | 192u);
    write32(GP0, (8u << 16) | 8u);
    for (int k = 0; k < 40000; k++) if (read32(GP1) & 0x04000000u) { ok[i] = 1; tm[i] = (uint16_t)(read16(T2_COUNT) - t0); break; }
}

int main() {
    printf("\ngpu/gpu-revive\n");
    printf("S step stat ok t. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    check(0);
    write32(GP1, 0x0B000000u); check(1);
    write32(GP1, 0x00000000u); check(2);
    write32(GP1, 0x01000000u); check(3);
    write32(GP1, 0x02000000u); check(4);
    write32(GP1, 0x0B000000u); write32(GP1, 0x01000000u); write32(GP1, 0x02000000u); write32(GP1, 0); check(5);
    write32(GP1, 0x0B000273u); write32(GP1, 0); check(6);
    write32(GP1, 0x0B000000u); write32(GP1, 0); check(7);
    waitT2(8000000); write32(GP1, 0); check(8);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int i = 0; i < 9; i++) printf("S %d %x %u %u\n", i, st[i], ok[i], tm[i]);
    printf("\ngpu/gpu-revive: Done.\n");
    for (;;) VSync(0);
}
