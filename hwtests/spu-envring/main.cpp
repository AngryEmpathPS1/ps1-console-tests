/*
 * spu/spu-envring - the envelope read as a waveform: every attack, decay, sustain and release setting, sample by sample (2026-11-01)
 *
 * WHY. hwtests/fuzz-spu reads the waveforms right and the envelope a step off, at a distance that changes with the rate: a sample at one setting, seven at another. ENVX read by the CPU
 * (spu-regs) cannot see which sample a step lands on. Voice 1's capture ring can: a voice playing a constant has the envelope for a waveform.
 *
 * WHAT IT DOES. Interrupts off, no disc, sound out muted. Voice 1 loops a block of 4000h. For each setting: keyed on just behind a half edge, the next two halves of the ring read back
 * (512 samples). ATTACK every value of ADSR1's top byte. DECAY every shift. SUSTAIN every value of ADSR2's top seven bits, behind the fastest attack and decay. RELEASE every shift and mode,
 * keyed off at a fixed distance. A line: A, D, S or R, the setting, then the first eight changes of the ring as index:value.
 *
 * READ IT LIKE THIS. The gap between changes is the rate's period, the step the rate's size, and the first change says where a period starts: at the key-on, or somewhere of its own.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-envring --frames=6000 --quiet=3000`.
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
static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint16_t cap[HALF * 2] __attribute__((aligned(16)));

static void run(char tag, uint32_t setting, uint16_t a1, uint16_t a2, int offAt) {
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);
    write16(SPU(0x10 + 0xA), 0x001F);
    idle(768u * 1100u);                                          /* silence in both halves */
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), a1);
    write16(SPU(0x10 + 0xA), a2);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    edge(1);
    write16(SPU(0x188), 0x0002);
    if (offAt >= 0) { idle((uint32_t)offAt * 768u); write16(SPU(0x18C), 0x0002); }
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap + HALF), HALF / 2, false);
    printf("%c %x", tag, setting);
    int n = 0;
    uint16_t last = 0;
    for (int i = 0; i < HALF * 2 && n < 8; i++) if (cap[i] != last) { printf(" %d:%x", i, cap[i]); last = cap[i]; n++; }
    printf("\n");
}

int main() {
    printf("\nspu/spu-envring\n");
    printf("Voice 1 playing 4000h, its capture ring: the setting, then the first eight changes. No assertions.\n\n");
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
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDma(0x1000, dcBlock, 16, true);
    for (uint32_t a = 0; a < 256; a++) run('A', a, (uint16_t)((a << 8) | 0x000F), 0x1FC0, -1);          /* attack; sustain level F, held */
    for (uint32_t d = 0; d < 16; d++) run('D', d, (uint16_t)(0x0000 | (d << 4) | 0x0), 0x1FC0, -1);     /* the fastest attack, decay d to sustain level 0 */
    for (uint32_t s = 0; s < 128; s++) run('S', s, 0x000F, (uint16_t)((s << 9) | 0x1F), -1);             /* the top seven of ADSR2, the mode and the sustain rate */
    for (uint32_t r = 0; r < 64; r++) run('R', r, 0x000F, (uint16_t)(0x1FC0 | r), 100);                  /* the low six of ADSR2, keyed off 100 samples on */
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-envring: Done.\n");
    for (;;) VSync(0);
}
