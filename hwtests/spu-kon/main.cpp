/*
 * spu/spu-kon - A GAME'S KEY-ONS WITH ITS OWN ENVX READS BEHIND THEM (2026-11-15; the console fuzzer session, for the SPU session:
 * "Crash's key-on bursts with the ADSR-level reads behind them - ENVX read back at the game's own delay after each key-on").
 *
 * WHAT IT DOES. experiments/fuzz-all/spukon.js records, from a .pss (Crash Bandicoot), each key-on with its voice's registers as the
 * SPU held them, the key-on's place in the SPU's 768-cycle sample, 4 KB of sound RAM from the voice's start, and the delays at which
 * the game read that voice's ENVX after it. Here, per case: voices off; the 4 KB put back at the voice's start (DMA4); the voice's
 * volume, pitch, start, ADSR and repeat written; then a SAMPLE BOUNDARY found - the SPU IRQ address at the voice-1 capture buffer's
 * start (800h), SPUCNT's IRQ bit on, I_STAT bit 9 polled: it rises when the capture position wraps to it, at a sample's tick (a
 * fixed latency after it; the same every case) - then the game's phase waited out, the key-on stored, and ENVX read at each of the
 * game's delays from that store (a busy-wait on Timer 2). Two passes.
 * The game's own stores to that voice after the key-on (its registers, its key-off) are replayed at their delays too.
 *   K <pass> <case> v <voice> phase <p> envx <value at each delay ...>
 * The game's own values (the core's, as it gave them to the game) are in spukon-<name>.json beside each case.
 * No assertions. THE RULE: run it here first: `node tools/hwrunner.js <exe> --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

extern "C" { extern char _end[]; }

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define SPU(o) R16(0x1F801C00u + (o))
#define T2 R16(0x1F801120u)
#define I_STAT R32(0x1F801070u)
#define SPUDELAY R32(0x1F801014u)

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = T2; clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static uint16_t spucnt;
static void spuMode(int m) { spucnt = (uint16_t)((spucnt & ~0x30) | (m << 4)); SPU(0x1AA) = spucnt; for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x3F) != (spucnt & 0x3F); i++) {} }
static void spuWrite(uint32_t spuAddr, const uint32_t* ram, uint32_t words) {
    spuMode(0); SPU(0x1A6) = (uint16_t)(spuAddr / 8); spuMode(2);
    R32(0x1F8010C0u) = (uint32_t)ram & 0xFFFFFF; R32(0x1F8010C4u) = ((words / 16) << 16) | 16; R32(0x1F8010C8u) = 0x01000201u;
    for (int i = 0; i < 2000000 && (R32(0x1F8010C8u) & 0x01000000u); i++) {}
    for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x0400); i++) {}
    idle(4000);
    spuMode(0);
}

int main() {
    printf("\nspu/spu-kon\nA game's key-ons at its own sample phase, ENVX read at its own delays behind each.\n");
    const uint32_t* tb = (const uint32_t*)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 8192 && tb[0] != 0x314B5053u; i++) tb += 4;
    if (tb[0] != 0x314B5053u) { printf("no table\nspu/spu-kon: Done.\n"); for (;;) VSync(0); }
    const uint32_t nC = tb[2];
    printf("%u cases\n\n", (unsigned)nC);
    EnterCriticalSection();
    const uint16_t m2 = R16(0x1F801124u), cnt0 = SPU(0x1AA), mv0 = SPU(0x180), mv1 = SPU(0x182), rb0 = SPU(0x1A2), irq0 = SPU(0x1A4);
    const uint32_t mask0 = R32(0x1F801074u), dpcr0 = R32(0x1F8010F0u);
    R32(0x1F801074u) = 0; R32(0x1F8010F0u) = dpcr0 | 0x00080000u;   /* DMA4 on: the runner leaves it off */
    R16(0x1F801124u) = 0; lastT = T2; clk = 0;
    SPU(0x180) = 0; SPU(0x182) = 0;
    SPU(0x1A2) = 0xFFFE;   /* the reverb's work area out of the way (hwtests/spu-replay's lesson) */
    static uint16_t vals[24];
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t* c = tb + 3;
        for (uint32_t i = 0; i < nC; i++) {
            const uint32_t voice = c[0] & 23, phase = c[1] % 768; const uint32_t* regs = c + 2; const uint32_t start = c[10], nR = c[11] < 24 ? c[11] : 24;
            const uint32_t* dl = c + 12; const uint32_t nE = dl[c[11]]; const uint32_t* ev = dl + c[11] + 1; const uint32_t* ram = ev + 3 * nE;
            SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
            spucnt = 0xC000; SPU(0x1AA) = spucnt; idle(2000);
            spuWrite(start, ram, 1024);
            const uint32_t vb = voice * 16;
            for (int r = 0; r < 8; r++) if (r != 6) SPU(vb + 2 * r) = (uint16_t)regs[r];
            /* a sample boundary: the capture buffer's wrap through the IRQ address */
            SPU(0x1A4) = 0x0100;
            spucnt = 0xC000; SPU(0x1AA) = spucnt; idle(100);
            I_STAT = ~0x200u;
            spucnt = 0xC040; SPU(0x1AA) = spucnt;
            uint32_t t0 = 0;
            { const uint32_t ts = now(); for (;;) { const uint32_t x = now(); if (I_STAT & 0x200u) { t0 = x; break; } if (x - ts > 2000000u) break; } }
            while (now() - t0 < 768u * 4 + phase) {}
            if (voice < 16) SPU(0x188) = (uint16_t)(1u << voice); else SPU(0x18A) = (uint16_t)(1u << (voice - 16));
            const uint32_t tk = now();
            /* the reads and the game's own stores to the voice, in time order */
            for (uint32_t k = 0, e = 0; k < nR;) {
                if (e < nE && ev[3 * e] <= dl[k]) { while (now() - tk < ev[3 * e]) {}
                    const uint32_t o = ev[3 * e + 1], v = ev[3 * e + 2];
                    if (o == 0x100) { if (voice < 16) SPU(0x18C) = (uint16_t)(1u << voice); else SPU(0x18E) = (uint16_t)(1u << (voice - 16)); }
                    else if (o != 0xC) SPU(vb + o) = (uint16_t)v;
                    e++; continue; }
                while (now() - tk < dl[k]) {}
                vals[k++] = SPU(vb + 0xC);
            }
            if (voice < 16) SPU(0x18C) = (uint16_t)(1u << voice); else SPU(0x18E) = (uint16_t)(1u << (voice - 16));
            spucnt = 0xC000; SPU(0x1AA) = spucnt; I_STAT = ~0x200u;
            ExitCriticalSection();
            printf("K %d %u v %u phase %u envx", pass, (unsigned)i, (unsigned)voice, (unsigned)phase);
            for (uint32_t k = 0; k < nR; k++) printf(" %x", (unsigned)vals[k]);
            printf("\n");
            EnterCriticalSection();
            c = ram + 1024;
        }
    }
    SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
    SPU(0x1A4) = irq0; SPU(0x1A2) = rb0; SPU(0x180) = mv0; SPU(0x182) = mv1; SPU(0x1AA) = cnt0;
    I_STAT = ~0x200u; R32(0x1F801074u) = mask0; R32(0x1F8010F0u) = dpcr0;
    R16(0x1F801124u) = m2;
    ExitCriticalSection();
    printf("\nspu/spu-kon: Done.\n");
    for (;;) VSync(0);
}
