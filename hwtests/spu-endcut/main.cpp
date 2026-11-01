/*
 * spu/spu-endcut - where a block's end flag lands, by pitch (2026-11-01)
 *
 * WHY. hwtests/fuzz-spu's rings agree sample for sample but where a sample ends: a block with the end flag cuts the voice 4 samples sooner than this core at pitch E9Fh, 2 at 11E9h, 1 at 1A9Ah. The
 * core acts on the flag as its decoder leaves the block, and the decoder's reads were fitted at 1000h.
 *
 * WHAT IT DOES. Interrupts off, no disc, sound out muted. Voice 1, a constant 4000h, the fastest attack held. N blocks, N of 1, 2, 3, 4, 5, 6, 8, 12. CUT: the last block's flags 1, the end with no repeat: the ring index of the
 * last sound. JUMP: flags 3, repeating to a block of 2000h: the ring index where the value first leaves 4000h. For 21 pitches from 0800h to 3000h. A line: C N pitch first last, J N pitch first change.
 *
 * READ IT LIKE THIS. index x pitch is the source position. A rule on the decoder's reads shows steps of four samples, a rule on the position does not.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-endcut --frames=6000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define HALF 256

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
}
static void edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
}
static uint32_t blocks[80] __attribute__((aligned(16)));
static uint16_t cap[HALF * 2] __attribute__((aligned(16)));

/* Five blocks at 1000h: three of 4000h, the third with `flag`; a block of 2000h and its copy, the repeat address. */
static int gN = 3;
static void setFlags(uint32_t flag) {
    uint8_t* b = (uint8_t*)blocks;
    for (int k = 0; k < 20; k++) {
        uint8_t* q = b + k * 16;
        q[0] = k < gN ? 0x00 : 0x01;
        q[1] = 0;
        for (int i = 2; i < 16; i++) q[i] = 0x44 >> 0;
    }
    b[(gN - 1) * 16 + 1] = (uint8_t)flag;
    b[gN * 16 + 1] = 0x04;
    b[(gN + 1) * 16 + 1] = 0x03;
    spuDma(0x1000, blocks, 80, true);
}
static void run(uint32_t pitch, uint32_t flag) {
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);
    write16(SPU(0x10 + 0xA), 0x001F);
    idle(768u * 600u);
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x4), (uint16_t)pitch);
    write16(SPU(0x10 + 0x8), 0x000F);
    write16(SPU(0x10 + 0xA), 0x1FC0);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    edge(1);
    write16(SPU(0x188), 0x0002);
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap + HALF), HALF / 2, false);
    int first = 0;
    while (first < HALF * 2 && !cap[first]) first++;
    int mark = -1;
    if (flag & 2) { int i = first; while (i < HALF * 2 && (int16_t)cap[i] < 0x3000) i++; for (; i < HALF * 2; i++) if ((int16_t)cap[i] < 0x3000) { mark = i; break; } }
    else { for (int i = HALF * 2 - 1; i >= 0; i--) if (cap[i]) { mark = i; break; } }
    printf("%c %d %x %d %d\n", (flag & 2) ? 'J' : 'C', gN, pitch, first, mark);
}

int main() {
    printf("\nspu/spu-endcut\n");
    printf("Voice 1, three blocks of 4000h ending in the end flag: C pitch first last, J pitch first change. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    static const int NS[8] = { 1, 2, 3, 4, 5, 6, 8, 12 };
    for (int cls = 0; cls < 2; cls++) {
        printf("CLASS %d\n", cls);
        for (int n = 0; n < 8; n += cls + 1) {
            gN = NS[n];
            setFlags(1);
            for (uint32_t p = 0x800; p <= 0x3000; p += 0x200) run(p, 1);
            setFlags(3);
            for (uint32_t p = 0x800; p <= 0x3000; p += 0x200) run(p, 3);
        }
        /* a sample with the transfer type not 2 moves the key-on's two-sample cycle (hwtests/spu-latch) */
        edge(0);
        write16(SPU(0x1AC), 0x0000);
        idle(768);
        write16(SPU(0x1AC), 0x0004);
    }
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-endcut: Done.\n");
    for (;;) VSync(0);
}
