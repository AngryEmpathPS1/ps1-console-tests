/*
 * spu/spu-afterreset - DOES THE CAPTURE RING RUN AFTER A HARDWARE RESET, AND WHICH WRITE STARTS IT? (2026-11-25, the SPU session)
 *
 * WHY. On the second SCPH-5501 (a cart console: the cart's boot runs hwtests/spu-bootprobe at its break, before the BIOS shell's
 * sound start) the probe kept after a hardware RESET (2026-11-25) and after the first power-ons with the cart (2026-11-17) reads
 * 32 key-ons with no onset in voice 1's capture and no step in any attack row, and the cart's own search found no sample clock in
 * SPUSTAT bit 11 - while 1F801DACh READ 0004h (the probe writes it only when it reads another value, and it did set SPUCNT
 * C000h). After a soft reset (no hardware reset) the same probe reads a whole log. Every later program that had run by the time
 * the ring was seen running had written 1F801DACh (the fuzz host does). So: after /RESET the ring stands still though the
 * register reads Normal, until the register is WRITTEN - or something else starts it. This says which.
 * It matters for the boot's draw (experiments/land_spuonedraw.js): a console's lead after a boot repeats to a sample, which needs
 * the ring to stand from the reset to the shell's one write of 0004h.
 *
 * WHAT IT DOES. Interrupts off. Nothing is written until stage 0 is printed. A stage: SPUSTAT bit 11's changes counted over 60 ms
 * (it turns every 256 samples, 5.8 ms, when the ring runs), and the first two changes' times.
 *   R 1dac 1daa 1dae 1d80 1d82 1014      the registers as found
 *   S 0 n t1 t2      as found, nothing written
 *   S 1 n t1 t2      after SPUCNT = C000h (the SPU on, unmuted; the main volume 0 first)
 *   S 2 n t1 t2      after 1F801DACh written with THE VALUE IT READ
 *   S 3 n t1 t2      after 1F801DACh = 0004h
 *   S 4 n t1 t2      after 1F801DACh = 0000h (a Fill type: the ring must stand - the test's own control), then 0004h again:
 *   S 5 n t1 t2
 * n 0 in a stage = the ring stood still through it. Every wait is bounded. No assertions. ~1 s.
 * RUN IT AS THE FIRST PROGRAM AFTER A RESET (a cart console: the runner is back by itself; the port held so no other job is first).
 * On a boot that has run anything else it reads the ring running at stage 0 - the same program is then its own control.
 * THE RULE: run here first: `node hwtests/run.js spu-afterreset --frames=600 --quiet=300`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t res[6][3];
static void stage(int k) {
    const uint32_t t0 = now();
    uint32_t n = 0, t1 = 0, t2 = 0;
    uint16_t last = read16(SPU(0x1AE)) & 0x0800;
    while (now() - t0 < 2032128u) {                                 /* 60 ms of CPU clock */
        const uint16_t b = read16(SPU(0x1AE)) & 0x0800;
        if (b != last) { last = b; n++; if (n == 1) t1 = now() - t0; else if (n == 2) t2 = now() - t0; }
    }
    res[k][0] = n; res[k][1] = t1; res[k][2] = t2;
}

int main() {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000); lastT = read16(T2_COUNT); clk = 0;
    const uint16_t dac = read16(SPU(0x1AC)), daa = read16(SPU(0x1AA)), dae = read16(SPU(0x1AE));
    const uint16_t vl = read16(SPU(0x180)), vr = read16(SPU(0x182));
    const uint32_t d14 = read32(0x1F801014);
    stage(0);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x1AA), 0xC000); idle(40000);
    stage(1);
    write16(SPU(0x1AC), dac); idle(40000);
    stage(2);
    write16(SPU(0x1AC), 0x0004); idle(40000);
    stage(3);
    write16(SPU(0x1AC), 0x0000); idle(40000);
    stage(4);
    write16(SPU(0x1AC), 0x0004); idle(40000);
    stage(5);
    const uint16_t dae2 = read16(SPU(0x1AE));
    write16(SPU(0x180), vl); write16(SPU(0x182), vr);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-afterreset\nDoes the capture ring run after a reset, and which write starts it: R registers / S stage changes t1 t2. No assertions.\n\n");
    printf("R %04x %04x %04x %04x %04x %08x\n", dac, daa, dae, vl, vr, (unsigned)d14);
    for (int k = 0; k < 6; k++) printf("S %d %u %u %u\n", k, (unsigned)res[k][0], (unsigned)res[k][1], (unsigned)res[k][2]);
    printf("E %04x\n", dae2);
    printf("\nspu/spu-afterreset: Done.\n");
    for (;;) VSync(0);
}
