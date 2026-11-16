/*
 * spu/spu-bootprobe - which SPU key-on class and envelope phase did THIS boot draw? (2026-11-16, the SPU session)
 *
 * WHY. Two SPU properties are per-boot draws, not per-console constants: the KEY-ON CLASS (which capture-ring parity takes a
 * key-on: v0.377.205 / .213, SPU.konParity, PSX_VC_SPUCLASS=flip) and the ENVELOPE PHASE (where the global envelope counter sits
 * against the sample clock: v0.377.277 / .300, PSX_VC_ENVPHASE). Both are the boot's history (transfer-control writes move them).
 * In one week they decided three results - Crash Bandicoot's key-on reads (the 1001 against the later boards), spu-dbuf's rows,
 * and spu-latch3 - each time found by hand. This probe, run in the same queue job right before an SPU test, says which the boot
 * drew; tools/spuboot.js reads its log against the emulator and writes the answer beside the log (spuboot.json).
 *
 * WHAT IT DOES. Interrupts off, sound muted, transfer control 0004h (Normal: it never moves either draw, so the probe leaves the
 * boot as it found it - nothing here writes 1F801DACh another value). Voice 1 loops a block of 4000h; its capture ring is read.
 *   K rows (32): voice 1 keyed on at a scattered time (Timer 2 stamp tw, 0-47 single cycles added), the ring read 120 samples on:
 *                K tw onset        onset = the ring index of its first sound (spu-latch2's K rows). Its parity is the class.
 *   A rows (9):  linear attack at shift 12..20 (a period of 2..512 samples a step), keyed on just behind a bit-11 half edge:
 *                A setting  i:v i:v ...   the ring's first eight changes (spu-envring's A rows). Where the steps fall is the phase.
 * Every wait is bounded. No assertions. Runs in ~6 s.
 * THE RULE: run here first: `node hwtests/run.js spu-bootprobe --frames=1500 --quiet=1000`.
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
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 400000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { printf("T %x\n", (unsigned)read32(0x1F8010C8)); write32(0x1F8010C8, 0); }
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

static void voice1(uint16_t a1, uint16_t a2) {
    write16(SPU(0x10 + 0x0), 0x3FFF); write16(SPU(0x10 + 0x2), 0x3FFF);
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), a1); write16(SPU(0x10 + 0xA), a2);
}
static void silence() {
    write16(SPU(0x10 + 0xA), 0x0000);
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);                               /* the fastest release (shift 0) */
    idle(768u * 1100u);                                             /* silence in both halves of the ring */
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
}
static void konRow() {
    silence();
    voice1(0x000F, 0x1FC0);
    idle(2000u + rnd(1536)); fine(rnd(48));
    const uint16_t t = read16(T2_COUNT);
    write16(SPU(0x188), 0x0002);
    const uint32_t tw = stamp(t);
    idle(120u * 768u);
    spuDma(0x0800, (uint32_t*)cap, 256, false);
    int onset = -1;
    for (int i = 0; i < 512 && onset < 0; i++) {
        if (!cap[i]) continue;
        int z = 0;
        while (z < 40 && !cap[(i + 511 - z) & 511]) z++;
        if (z == 40) onset = i;
    }
    now(); printf("K %u %d\n", (unsigned)tw, onset); now();
}
static void attackRow(uint32_t a) {
    silence();
    voice1((uint16_t)((a << 8) | 0x000F), 0x1FC0);
    edge(1);
    write16(SPU(0x188), 0x0002);
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap + HALF), HALF / 2, false);
    now(); printf("A %x", (unsigned)a);
    int n = 0; uint16_t last = 0;
    for (int i = 0; i < HALF * 2 && n < 8; i++) if (cap[i] != last) { now(); printf(" %d:%x", i, cap[i]); last = cap[i]; n++; }
    now(); printf("\n"); now();
}

int main() {
    printf("\nspu/spu-bootprobe\n");
    printf("This boot's SPU key-on class and envelope phase: K tw onset / A setting i:v ... (tools/spuboot.js reads it). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    const uint16_t dtc = read16(SPU(0x1AC));
    if (dtc != 0x0004) write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDma(0x1000, dcBlock, 16, true);
    printf("D %04x\n", dtc);                                        /* the transfer control the boot had (0004h keeps the draws) */
    for (int n = 0; n < 32; n++) konRow();
    for (uint32_t s = 12; s <= 20; s++) attackRow(s << 2);          /* linear attack, step 0, shift s */
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-bootprobe: Done.\n");
    for (;;) VSync(0);
}
