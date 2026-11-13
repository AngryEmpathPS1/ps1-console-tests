/*
 * spu/spu-readback - WHAT a long DMA4 read of sound RAM returns, halfword by halfword (2026-11-13)
 *
 * WHY. The whole-console fuzzer's host (hwtests/fuzz-all) reads sound RAM 0-1FFFh back by DMA4 after every case, and on the
 * second SCPH-5501 that read differs from this emulator's in nearly every case, SPU ops or none (STATUS "The whole-console
 * fuzzer", family 4): alternating 256-byte blocks, and in the all-zero capture area the console's block hashes are five values
 * repeating every nine blocks - something non-zero arrives about every 230 halfwords. The hashes do not say what or where.
 *
 * WHAT IT DOES. Interrupts off, sound out muted. Sound RAM 1000h-1FFFh is written (DMA4) with each halfword's own index | A000h,
 * so a halfword that arrives in the wrong place names where it came from. Then 0-1FFFh is read by DMA4 exactly as the fuzzer's
 * host reads it (transfer mode 0, the address, mode 3 polled, BCR 16-word blocks, CHCR 01000200h), three passes in a row, and
 * every halfword that is not the expected one (0 below 1000h, the index pattern above) is printed: "X mode pass index got".
 * A line "P mode pass cycles count" closes each pass (cycles: CHCR write to the channel's end, Timer 2).
 * Mode C: the voices as the runner left them. Mode F: the fuzzer's setup first (every voice keyed on at 2000h, released, keyed
 * off, registers from a fixed seed). Mode Z: every voice's pitch 0 and volume 0 (no voice moves through sound RAM).
 * Mode B8 / B4 (built only with -DSMALL_BLOCKS): mode C with 8- and 4-word blocks - kept out of the default build until the
 * 16-word modes (what the fuzzer's host does on every case) have run on the consoles. The first build's run on the second
 * SCPH-5501 returned nothing in 600 s (2026-11-13), but the console had been switched off for another job: not evidence of a
 * hang. A DMA4 read still live at its wait's end is stopped and printed as "T chcr madr bcr", so it cannot write on.
 * Mode S: mode Z with 1F801014h = 220931E1h (psx-spx's stable read; every
 * mode before it reads with the value the BIOS left, printed as "D value" - 200931E1h, psx-spx's "unstable reading", which
 * is what the fuzzer's host does).
 *
 * There are no assertions - compare against your emulator.
 * THE RULE: run it here first: `node hwtests/run.js spu-readback`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define D4_MADR 0x1F8010C0u
#define D4_BCR  0x1F8010C4u
#define D4_CHCR 0x1F8010C8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4)); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static uint32_t spuDma(uint32_t spuAddr, uint32_t* ram, uint32_t words, uint32_t bs, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(D4_MADR, (uint32_t)ram & 0xFFFFFF);
    write32(D4_BCR, ((words / bs) << 16) | bs);
    const uint32_t t0 = now();
    write32(D4_CHCR, toSpu ? 0x01000201u : 0x01000200u);
    int i = 0;
    for (; i < 200000 && (read32(D4_CHCR) & 0x01000000); i++) { now(); }
    const uint32_t t1 = now();
    if (read32(D4_CHCR) & 0x01000000) {                             /* never left running: a channel still live would write on */
        printf("T %x %x %x\n", (unsigned)read32(D4_CHCR), (unsigned)read32(D4_MADR), (unsigned)read32(D4_BCR));
        write32(D4_CHCR, 0);
    }
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0400); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
    return t1 - t0;
}
static uint32_t buf[2048] __attribute__((aligned(16)));
static uint32_t rng = 0x1234567;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void fill() {
    uint16_t* h = (uint16_t*)buf;
    for (uint32_t i = 0; i < 2048; i++) h[i] = (uint16_t)(0xA000 | (0x800 + i));
    spuDma(0x1000, buf, 1024, 16, true);
}
static void passes(const char* mode, uint32_t bs) {
    for (int p = 0; p < 3; p++) {
        uint16_t* h = (uint16_t*)buf;
        for (uint32_t i = 0; i < 4096; i++) h[i] = 0xEEEE;
        const uint32_t t = spuDma(0x0000, buf, 2048, bs, false);
        int n = 0;
        for (uint32_t i = 0; i < 4096; i++) {
            const uint16_t want = i < 0x800 ? 0 : (uint16_t)(0xA000 | i);
            if (h[i] != want) { if (n < 120) printf("X %s %d %u %04x\n", mode, p, (unsigned)i, h[i]); n++; }
        }
        printf("P %s %d %u %d\n", mode, p, (unsigned)t, n);
    }
}
static void fuzzSetup() {
    for (uint32_t v = 0; v < 24; v++) write16(SPU(v * 16 + 0xA), 0);
    for (uint32_t v = 0; v < 24; v++) { write16(SPU(v * 16 + 0x6), 0x2000 / 8); write16(SPU(v * 16 + 0x4), 0x1000); write16(SPU(v * 16 + 0x8), 0x000F); }
    write16(SPU(0x188), 0xFFFF); write16(SPU(0x18A), 0x00FF);
    idle(4000);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(4000);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    for (uint32_t r = 0x190; r < 0x19C; r += 2) write16(SPU(r), 0);
    for (uint32_t v = 0; v < 24; v++) {
        write16(SPU(v * 16 + 0x0), (uint16_t)(rnd() & 0x3FFF));
        write16(SPU(v * 16 + 0x2), (uint16_t)(rnd() & 0x3FFF));
        write16(SPU(v * 16 + 0x4), (uint16_t)(rnd() & 0x3FFF));
        write16(SPU(v * 16 + 0x6), (uint16_t)(0x200 + (rnd() & 0x1FF)));
        write16(SPU(v * 16 + 0x8), (uint16_t)rnd());
        write16(SPU(v * 16 + 0xA), (uint16_t)rnd());
        write16(SPU(v * 16 + 0xE), (uint16_t)(0x200 + (rnd() & 0x1FF)));
    }
}

int main() {
    printf("\nspu/spu-readback\n");
    printf("Sound RAM 0-1FFFh read by DMA4, three passes a mode: X mode pass index got (each unexpected halfword), P mode pass cycles count. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x1B0), 0); write16(SPU(0x1B2), 0); write16(SPU(0x1B4), 0); write16(SPU(0x1B6), 0);
    write16(SPU(0x1AC), 0x0004);
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    fill();
    idle(200000);
    passes("C", 16);
#ifdef SMALL_BLOCKS
    passes("B8", 8);
    passes("B4", 4);
#endif
    fuzzSetup();
    fill();
    idle(200000);
    passes("F", 16);
    for (uint32_t v = 0; v < 24; v++) { write16(SPU(v * 16 + 0x0), 0); write16(SPU(v * 16 + 0x2), 0); write16(SPU(v * 16 + 0x4), 0); }
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(200000);
    passes("Z", 16);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                                /* the stable read (psx-spx: bits 24-27 nonzero) */
    passes("S", 16);
    write32(0x1F801014, spuDelay);
    printf("D %08x\n", (unsigned)spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-readback: Done.\n");
    for (;;) VSync(0);
}
