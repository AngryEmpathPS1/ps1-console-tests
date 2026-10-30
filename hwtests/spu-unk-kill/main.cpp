/*
 * spu/spu-unk-kill - which of spu-curvol's writes to the undocumented SPU registers stops the SCPH-7501's capture.
 *
 * WHY (2026-10-09, console day part 2). spu-curvol on the freshly reset 7501 left its SPU with voice 1's capture ring
 * all zeros and the SPU IRQ never firing (its own IRQ rows -1), and spu-path after it read 0 of 512 - as the night of
 * 2026-10-08, when spu-curvol ran in the batch and every capture-reading SPU test after it read zeros. On the 5501 the same
 * program fired its IRQ and its ring carried the voice. spu-curvol's first act is writing patterns into 1DBCh, 1DBEh,
 * 1E60h-1E7Eh (each put back), 1DA0h and 1E80h-1E86h - registers psx-spx calls general-purpose RAM with no effect on
 * sound (nocash's console). The 7501's SPU is the CXD2938Q, SPU and CD in one chip.
 *
 * WHAT IT DOES. spu-path's setup and helpers; the capture checked (voice 1 off a whole ring - zeros - then on a loud
 * loop a whole ring - nonzero) before anything, then after each of spu-curvol's writes in its order, stopping at the
 * first DEAD line. The last line printed alive and the DEAD one after it bracket the register.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-unk-kill --ntsc`. A console that goes DEAD needs a RESET.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static void manualWrite(uint32_t a, const uint16_t *d, int n) {
    setCnt(cnt & ~0x30);
    w16(0x1A6, a / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, d[i]);
    setCnt((cnt & ~0x30) | 0x10);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {}
    setCnt(cnt & ~0x30);
}
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    { const uint32_t t0 = now(); while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 2000000) {} }
    setCnt(cnt & ~0x30);
}

static uint16_t buf[1024] __attribute__((aligned(16)));

/* Is voice 1's capture alive? Voice 1 off for 450,000 cycles (a whole ring of a silent voice), the ring read; then on
 * (a loud loop) for 450,000, the ring read. Alive reads 0 then ~512 nonzero; a capture writing zeros 0 and 0; one that
 * stopped writing the same stale count twice. */
static int alive(const char* label) {
    w16(0x18C, 0x0002); w16(0x18E, 0x0000);
    waitCycles(450000);
    dmaRead(0x0800, buf, 1024);
    int off = 0; for (int i = 0; i < 512; i++) if (buf[i]) off++;
    w16(0x10, 0x3FFF); w16(0x12, 0x3FFF); w16(0x14, 0x1000); w16(0x16, 0x2000 / 8); w16(0x1E, 0x2000 / 8);
    w16(0x18, 0x00FF); w16(0x1A, 0x0000);
    w16(0x188, 0x0002); w16(0x18A, 0x0000);
    waitCycles(450000);
    const uint16_t env = r16(0x1C);
    dmaRead(0x0800, buf, 1024);
    int on = 0; for (int i = 0; i < 512; i++) if (buf[i]) on++;
    printf("  %-28s off %3d  on %3d of 512 nonzero  ENVX %04x  %s\n", label, off, on, env, (off == 0 && on > 400) ? "alive" : "DEAD");
    return off == 0 && on > 400;
}

int main() {
    printf("\nspu/spu-unk-kill\n");
    printf("Which undocumented SPU register stops voice 1's capture? spu-curvol's writes one at a time. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    static uint16_t smp[16];
    for (int b = 0; b < 2; b++) {
        smp[b * 8] = (uint16_t)(0x0000 | ((b == 0 ? 0x04 : 0x03) << 8));
        for (int i = 1; i < 8; i++) smp[b * 8 + i] = 0x7777;
    }
    manualWrite(0x2000, smp, 16);
    for (int v = 0; v < 24; v++) { const uint32_t b = v * 16; w16(b + 0, 0); w16(b + 2, 0); w16(b + 8, 0); w16(b + 0xA, 0); w16(b + 0xC, 0); }
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    printf("  found: 1DA0 %04x  1DBC %04x %04x  1E60 %04x %04x ... 1E7E %04x  1E80 %04x\n",
           r16(0x1A0), r16(0x1BC), r16(0x1BE), r16(0x260), r16(0x262), r16(0x27E), r16(0x280));
    static const uint16_t PAT[4] = { 0x0000, 0xFFFF, 0x5A5A, 0xA5A5 };
    char label[40];
    bool ok = alive("before anything");
    /* spu-curvol's order: 1DBC, 1DBE, 1E60..1E7E (patterns, the old value put back), then 1DA0's four, then 1E80..1E86 */
    for (int k = 0; k < 18 && ok; k++) {
        const uint32_t o = k == 0 ? 0x1BC : k == 1 ? 0x1BE : 0x260 + 2 * (k - 2);
        const uint16_t old = r16(o);
        uint16_t rb[4];
        for (int p = 0; p < 4; p++) { w16(o, PAT[p]); rb[p] = r16(o); }
        w16(o, old);
        sprintf(label, "%04Xh (read %04x %04x %04x %04x)", (unsigned)(0x1C00 + o), rb[0], rb[1], rb[2], rb[3]);
        ok = alive(label);
    }
    for (int p = 0; p < 4 && ok; p++) {
        w16(0x1A0, PAT[p]);
        sprintf(label, "1DA0h = %04x (reads %04x)", PAT[p], r16(0x1A0));
        ok = alive(label);
    }
    for (int k = 0; k < 4 && ok; k++) {
        const uint16_t before = r16(0x280 + 2 * k);
        w16(0x280 + 2 * k, 0x1234);
        sprintf(label, "1E%02Xh = 1234 (%04x -> %04x)", 0x80 + 2 * k, before, r16(0x280 + 2 * k));
        ok = alive(label);
    }
    printf("  %s\n", ok ? "every write made: the capture lived through all of them" : "stopped at the first DEAD line above");
    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-unk-kill: Done.\n");
    for (;;) VSync(0);
}
