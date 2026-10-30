/*
 * dma/dma-arb - what the CPU's own bus accesses cost a block-mode DMA, one kind of access at a time.
 *
 * WHY (2026-10-08, the consensus over console day's logs). Two tests found the CPU slowing a DMA where this core
 * does not, and neither can say which access does it:
 *   - dma-contention (DMA2, 8,192 words to VRAM): CPU stores slow the transfer +1,504..1,569 cycles on the three
 *     consoles (here +539) and get 35% of the ALU loop's turns done (here 62%); its lw/lw/sw loop slows it LESS on
 *     the consoles (15,474) than here (17,063), and takes 60.4 cycles a step during the DMA (here 53.3).
 *   - concurrent-dma (DMA4, 128 blocks of 16 words to sound RAM): with the CPU sampling five DMA/timer registers and
 *     storing each sample to RAM, DMA4 takes ~162,700 cycles on all three consoles, here 146,400 - 127 cycles a block
 *     more - where dma-rates' DMA4 with the CPU doing ALU work and one CHCR read a turn takes the consoles' time here
 *     to 0.1% (72,796 / 72,734 for 64 blocks).
 * So something the CPU does in that loop - an I/O read, a RAM store, a RAM load, an uncached fetch - holds the bus a
 * block-mode DMA wants, and this core charges it differently. The model needs the cost of each kind of access alone.
 *
 * WHAT IT DOES. For each channel - GPU (DMA2, a 128x128 CPU->VRAM upload, 512 blocks of 16 words, as dma-contention)
 * and SPU (DMA4, 128 blocks of 16 words to sound RAM 1000h, as concurrent-dma) - the transfer is run once per CPU
 * workload, inside measure() (interrupts off, Timer 2 reset). The CPU starts the channel (CHCR store), then repeats a
 * TURN until CHCR's busy bit clears: n UNITS of one kind (inline assembly, its instructions exact; n chosen so a turn is
 * ~250-400 cycles), one read of Timer 2, one of CHCR - so the transfer's end is bracketed to a turn: after the last
 * turn that read it busy, by the turn that read it idle. Units:
 *   SPIN    nothing - the timer and CHCR reads alone each turn (the control: what dma/chopping does)
 *   ALU     one addu (the bus untouched; the loop's own 3 instructions a unit as in every case)
 *   STk     one sw to RAM, then k addu (k = 0, 2, 8, 32: how dense the stores are)
 *   LDk     one lw from RAM, then k addu
 *   SPAD    one sw to the scratchpad (on the chip: the control for STk)
 *   IOR     one lw of DPCR (an on-chip DMA register)
 *   TMR     one lw of Timer 2's count
 *   SPUR    one lhu of SPUSTAT (the SPU's bus - DMA4's own target)
 *   UNC     ALU's unit with its loop run UNCACHED (its KSEG1 alias: every fetch a RAM read)
 *   SAMPLE  concurrent-dma's sampling body: two CHCRs, Timer 2, two MADRs read, the sample stored to RAM (4 stores)
 * Then every workload WITHOUT a DMA (64 turns), for what a unit costs the CPU alone. GPU times are to the cycle, SPU
 * times to 8 (Timer 2 at the system clock / 8 there: measure() corrects for one wrap only, and its first version timed
 * the SPU to the cycle and read it 65,536 short).
 *
 * READ IT LIKE THIS. Against SPIN, a workload's later end is what its accesses take from the DMA; its units against its
 * cost alone is what the DMA takes from the CPU. ST0..ST32 say whether a store costs the DMA by the access (the same
 * per store at any density) or by the write buffer filling (dense stores only); LD0..LD32 the same for loads; IOR, TMR
 * and SPUR whether an on-chip or other-bus register read is free; UNC whether instruction fetches count as loads.
 * SAMPLE should give concurrent-dma's DMA4 time on a console (~162,700): the control that the cause is in here.
 *
 * ON THIS CORE AT v0.377.41: a unit alone ALU 4.35, ST0 4.34 (the write buffer takes it), ST2/8/32 6.46 / 12.94 / 37.44,
 * LD0 9.71 (a RAM load ~5.4 more), SPAD 4.36, IOR / TMR 7.47, SPUR 23.15, UNC 28.44, SAMPLE 206.09. GPU: SPIN ends by
 * 14,129; stores +840 / +1,560 / +1,050 / +600 (ST0..ST32), loads +190 / +90 / +360 / +520, SPAD +140, IOR +55, TMR +100,
 * SPUR +125, UNC +75, SAMPLE +650. SPU: SPIN by 146,528 and every workload within +470 (ST32's), SAMPLE +80 - so
 * concurrent-dma's ~16,000 on the consoles is in none of this core's accesses.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-arb`.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <gpu.h>
#include <io.h>
#include <malloc.h>
#include <dma.hpp>
#include <measure.hpp>

using namespace DMA;

#define GPU_W 128
#define GPU_H 128
#define GPU_WORDS (GPU_W * GPU_H / 2)      /* 8,192 */
#define SPU_BLOCKS 128
#define SPU_WORDS (SPU_BLOCKS * 16)
#define DPCR_ADDR 0x1F8010F0u
#define T2_COUNT  0x1F801120u
#define SPU(o) (0x1F801C00u + (o))
#define MADR(ch) (0x1F801080u + (ch) * 0x10)
#define BCR(ch)  (0x1F801084u + (ch) * 0x10)
#define CHCR(ch) (0x1F801088u + (ch) * 0x10)

static uint32_t* gpuSrc;
static uint32_t spuSrc[SPU_WORDS];
static volatile uint32_t ramCell __attribute__((aligned(16)));
struct Sample { uint16_t t; uint8_t busy; uint32_t m2, m4; };
static Sample samples[64];
static volatile uint32_t sink;

/* ---- the units, n a turn, each its exact instructions ---- */
#define LOOP_OPEN  ".set noreorder\n move $t4, %[n]\n 1:\n"
#define LOOP_CLOSE " addiu $t4, $t4, -1\n bnez $t4, 1b\n nop\n .set reorder\n"
#define ADDS(k)    ".rept " #k "\n addu $t2, $t2, $t3\n .endr\n"
#define CLOB       "t2", "t3", "t4", "t5", "memory"
#define UNIT(fname, body, addr) static void __attribute__((noinline, aligned(64))) fname(int n) { \
    __asm__ volatile(LOOP_OPEN body LOOP_CLOSE :: [n] "r"(n), [a] "r"(addr) : CLOB); }

UNIT(uAlu,  ADDS(1), 0)
UNIT(uSt0,  " sw $t2, 0(%[a])\n", &ramCell)
UNIT(uSt2,  " sw $t2, 0(%[a])\n" ADDS(2), &ramCell)
UNIT(uSt8,  " sw $t2, 0(%[a])\n" ADDS(8), &ramCell)
UNIT(uSt32, " sw $t2, 0(%[a])\n" ADDS(32), &ramCell)
UNIT(uLd0,  " lw $t5, 0(%[a])\n", &ramCell)
UNIT(uLd2,  " lw $t5, 0(%[a])\n" ADDS(2), &ramCell)
UNIT(uLd8,  " lw $t5, 0(%[a])\n" ADDS(8), &ramCell)
UNIT(uLd32, " lw $t5, 0(%[a])\n" ADDS(32), &ramCell)
UNIT(uSpad, " sw $t2, 0(%[a])\n", 0x1F800100u)
UNIT(uIor,  " lw $t5, 0(%[a])\n", DPCR_ADDR)
UNIT(uTmr,  " lw $t5, 0(%[a])\n", T2_COUNT)
UNIT(uSpur, " lhu $t5, 0(%[a])\n", SPU(0x1AE))
static void uUnc(int n) { ((void (*)(int))(((uint32_t)uAlu) | 0xA0000000u))(n); }
/* concurrent-dma's sample (its loop's body), into a 64-entry ring */
static void __attribute__((noinline)) uSample(int k) {
    for (int n = 0; n < k; n++) {
        const uint32_t c2 = read32(CHCR(2)), c4 = read32(CHCR(4));
        Sample& s = samples[n & 63];
        s.t = (uint16_t)read32(T2_COUNT);
        s.m2 = read32(MADR(2)) & 0xFFFFFF;
        s.m4 = read32(MADR(4)) & 0xFFFFFF;
        s.busy = ((c2 >> 24) & 1) | (((c4 >> 24) & 1) << 1);
    }
}
static void uSpin(int) {}

/* Units a turn: a turn of ~250-400 cycles, so the CPU sees the transfer end within that (and every turn is timed). */
struct Work { const char* name; void (*fn)(int); int per; };
static const Work works[] = {
    { "SPIN", uSpin, 0 }, { "ALU", uAlu, 64 },
    { "ST0", uSt0, 64 }, { "ST2", uSt2, 48 }, { "ST8", uSt8, 24 }, { "ST32", uSt32, 16 },
    { "LD0", uLd0, 32 }, { "LD2", uLd2, 32 }, { "LD8", uLd8, 24 }, { "LD32", uLd32, 16 },
    { "SPAD", uSpad, 64 }, { "IOR", uIor, 48 }, { "TMR", uTmr, 48 }, { "SPUR", uSpur, 16 }, { "UNC", uUnc, 12 },
    { "SAMPLE", uSample, 1 },
};
#define NWORK (int)(sizeof(works) / sizeof(works[0]))

/* ---- the two channels ---- */
static void spuMode(uint16_t mode) {
    const uint16_t cnt = (read16(SPU(0x1AA)) & ~0x30) | (mode << 4);
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { }
}
static void armGpu(void) {
    writeGP1(1, 0);
    DrawSync(0);
    CPU2VRAM buf;
    setcode(&buf, 0xA0);
    setlen(&buf, 3);
    setXY0(&buf, 0, 0);
    setWH(&buf, GPU_W, GPU_H);
    DrawPrim(&buf);
    writeGP1(4, 2);
    write32(MADR(2), (uint32_t)gpuSrc & 0xFFFFFF);
    write32(BCR(2), ((GPU_WORDS / 16) << 16) | 16);
}
static void armSpu(void) {
    spuMode(0);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x1A6), 0x1000 / 8);
    spuMode(2);
    write32(MADR(4), (uint32_t)spuSrc & 0xFFFFFF);
    write32(BCR(4), (SPU_BLOCKS << 16) | 16);
}

static int g_ch, g_per;
static void (*g_fn)(int);
static volatile uint32_t g_turns, g_lastBusy, g_end;

/* One transfer under one workload. Each turn reads Timer 2, then CHCR: the transfer ended after the last turn that
 * read it busy (g_lastBusy) and by the turn that read it idle (g_end). */
static void runOne(int ch, const Work& w, uint32_t* turns, uint32_t* lo, uint32_t* hi) {
    if (ch == 2) armGpu(); else armSpu();
    g_ch = ch; g_fn = w.fn; g_per = w.per; g_turns = 0;
    auto body = []() {
        const uint32_t c = CHCR(g_ch);
        write32(c, 0x01000201u);                 /* from RAM, SyncMode 1, start */
        uint32_t n = 0, t, last = 0, b;
        do { g_fn(g_per); n++; t = read32(T2_COUNT) & 0xFFFF; b = read32(c) & (1u << 24); if (b) last = t; } while (b);
        g_turns = n; g_lastBusy = last; g_end = t;
    };
    const uint32_t scale = ch == 2 ? 1 : 8;
    if (ch == 2) measure<MeasureMethod::CpuClock>(body); else measure<MeasureMethod::CpuClock8>(body);
    if (ch == 4) spuMode(0);
    *turns = g_turns; *lo = g_lastBusy * scale; *hi = g_end * scale;
}
static uint32_t idleCost(const Work& w) {
    g_fn = w.fn; g_per = w.per;
    return measure<MeasureMethod::CpuClock>([]() { for (int i = 0; i < 64; i++) g_fn(g_per); });
}

static void print2(uint32_t num, uint32_t den) {             /* num/den with two decimals */
    if (!den) { printf("    -   "); return; }
    printf("%5d.%02d", (int)(num / den), (int)((100 * (num % den)) / den));
}

int main() {
    printf("\ndma/dma-arb\n");
    printf("A block-mode DMA (GPU 8,192 words, SPU 2,048) with the CPU doing one kind of bus access at a time between\n");
    printf("CHCR reads. No assertions - compare against your emulator, and say which console ran it.\n\n");
    gpuSrc = (uint32_t*)malloc(GPU_WORDS * 4);
    for (int i = 0; i < GPU_WORDS; i++) gpuSrc[i] = 0x7FFF0000u + i;
    for (int i = 0; i < SPU_WORDS; i++) spuSrc[i] = 0x00010001u * (i & 0x7FFF);
    write32(DPCR_ADDR, read32(DPCR_ADDR) | (8u << 8) | (8u << 16));   /* DMA2 and DMA4 enabled */

    printf("CPU alone (64 turns): cycles a unit\n ");
    for (int w = 1; w < NWORK; w++) { printf(" %s ", works[w].name); print2(idleCost(works[w]), 64 * works[w].per); if (w % 5 == 0) printf("\n "); }
    printf("\n\n");

    const int chans[2] = { 2, 4 };
    for (int c = 0; c < 2; c++) {
        printf("%s (DMA%d): workload, units a turn, the end (after - by, cycles), by against SPIN, turns, units\n",
               chans[c] == 2 ? "GPU" : "SPU", chans[c]);
        uint32_t spin = 0;
        for (int w = 0; w < NWORK; w++) {
            uint32_t turns, lo, hi;
            runOne(chans[c], works[w], &turns, &lo, &hi);
            if (w == 0) spin = hi;
            printf("  %-7s %3d  %7d - %7d  %+6d  %6d  %7d\n", works[w].name, works[w].per, (int)lo, (int)hi, (int)hi - (int)spin,
                   (int)turns, (int)turns * works[w].per);
        }
        printf("\n");
    }
    sink = samples[0].t;
    printf("dma/dma-arb: Done.\n");
    for (;;) VSync(0);
}
