/*
 * mdec/mdec-glitch24c - HOW LONG after its Y4 the next macroblock's Y1 is there for the 24bpp glitch, in cycles (2026-11-25, the MDEC
 * session; STATUS "The 24bpp glitch is the CPU's alone").
 *
 * WHY. mdec-glitch24 and -glitch24b on the consoles: a CPU read of word 16 or 40 of a 24bpp quadrant takes the next quadrant's low
 * halfword once that quadrant is decoded. Reading at full pace (73,234 cycles for 768 words) a macroblock's last quadrant's word 16
 * reads its own; 8 cycles a read (122,567) and slower, the next macroblock's. On this core those two reads are 2,462 and 4,042
 * cycles after the last quadrant landed: the edge is between them.
 *
 * WHAT IT DOES. mdec-glitch24's blocks, input by DMA0, output read by the CPU at full pace, with D cycles of wait before reads 160,
 * 352 and 544 only (the word 16 of quadrants 3, 7 and 11), D = 0, 128, 256 ... 1792; STEP is the control as before.
 * READ IT LIKE THIS: a way's q3 / q7 / q11 w16 equal to STEP's is past the edge; listed (its own) before it.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>
#include <io.h>
#include <dma.hpp>
#include <mdec.h>
#include "symbols.h"

using namespace DMA;

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define WORDS 768                                   /* four 16x16 macroblocks at 24bpp: 3,072 bytes */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void wait(uint32_t cycles) { if (!cycles) return; const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint32_t input[256];                         /* symbols_mdec, word-aligned for DMA0 */
static uint32_t stepOut[WORDS], out[WORDS];
static const uint32_t CMD = (1u << 29) | ((uint32_t)ColorDepth::bit_24 << 27) | 256u;

static void prepare() {
    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable((int16_t*)idct);
    mdec_enableDma();
}

/* Every wait on the MDEC is bounded (2026-10-08): mdec-color, the overnight SCPH-5501 batch's first MDEC test, printed
 * nothing for four minutes - a decoder that stops leaves a test silent, and a silent test is a console the PC gives up
 * on. A wait that runs out says where, with the MDEC's status, and the test goes on. */
static bool cmdIdle() { const uint32_t t0 = now(); while (mdec_cmdBusy()) if (now() - t0 > 33869000u) { printf("  (the MDEC stayed busy 1 s: status %08x)\n", (unsigned)read32(MDEC_STATUS)); return false; } return true; }

/* mdec-step24's order, every access N cycles after the last. */
static void runStep(uint32_t n, uint32_t* dst) {
    prepare();
    if (!cmdIdle()) return;
    write32(MDEC_CMD, CMD);
    int in = 0, o = 0;
    uint32_t moved = now();
    while (in < 256 || o < WORDS) {
        const int in0 = in, o0 = o;
        while (in < 256) { wait(n); if (mdec_dataInFifoFull()) break; write32(MDEC_CMD, input[in++]); }
        if (in < 256 || o < WORDS) while (o < WORDS) { wait(n); if (mdec_dataOutFifoEmpty()) break; dst[o++] = read32(MDEC_DATA); }
        if (in != in0 || o != o0) moved = now();
        else if (now() - moved > 33869000u + 2 * n) {   /* nothing in or out for a second past the pace */
            printf("  STEP STOPPED: %d of 256 words in, %d of %d out, status %08x\n", in, o, WORDS, (unsigned)read32(MDEC_STATUS));
            return;
        }
    }
}

static void startDma0() {
    masterEnable(Channel::MDECin, true);
    write32(CH_BASE_ADDR    + 0x10 * (int)Channel::MDECin, (uint32_t)input & 0xFFFFFF);
    write32(CH_BLOCK_ADDR   + 0x10 * (int)Channel::MDECin, BCR::mode1(0x20, 256 / 0x20)._reg);
    write32(CH_CONTROL_ADDR + 0x10 * (int)Channel::MDECin, CHCR::MDECin()._reg);
}

/* Input by DMA0, output by the CPU, n cycles before each read. */
static uint32_t cpuTime;
static void runCpu(uint32_t n, uint32_t* dst) {
    const uint32_t d = n;   /* glitch24c: n is the extra wait before reads 160, 352, 544 */
    prepare();
    if (!cmdIdle()) return;
    write32(MDEC_CMD, CMD);
    startDma0();
    const uint32_t tStart = now();
    for (int o = 0; o < WORDS; o++) {
        if (o == 160 || o == 352 || o == 544) wait(d);
        const uint32_t t0 = now();
        while (mdec_dataOutFifoEmpty()) if (now() - t0 > 33869000u) { dst[o] = 0xDEADDEAD; return; }   /* 1 s: stuck */
        dst[o] = read32(MDEC_DATA);
    }
    cpuTime = now() - tStart;
}

/* Input by DMA0, output by DMA1 in blocks of bs words. */
static bool runDma(uint32_t bs, uint32_t* dst) {
    prepare();
    if (!cmdIdle()) return false;
    write32(MDEC_CMD, CMD);
    masterEnable(Channel::MDECout, true);
    write32(CH_BASE_ADDR    + 0x10 * (int)Channel::MDECout, (uint32_t)dst & 0xFFFFFF);
    write32(CH_BLOCK_ADDR   + 0x10 * (int)Channel::MDECout, BCR::mode1(bs, WORDS / bs)._reg);
    write32(CH_CONTROL_ADDR + 0x10 * (int)Channel::MDECout, CHCR::MDECout()._reg);
    startDma0();
    const uint32_t t0 = now();
    while (read32(CH_CONTROL_ADDR + 0x10 * (int)Channel::MDECout) & (1u << 24)) if (now() - t0 > 33869000u) return false;
    return true;
}

static void showDiff(const char* name) {
    int n = 0;
    for (int i = 0; i < WORDS; i++) if (out[i] != stepOut[i]) n++;
    printf("%s: %d words differ from STEP\n", name, n);
    int shown = 0;
    for (int i = 0; i < WORDS && shown < 48; i++)
        if (out[i] != stepOut[i]) { printf("    q%-2d w%-2d  %08x  (STEP %08x)\n", i / 48, i % 48, (unsigned)out[i], (unsigned)stepOut[i]); shown++; }
}

int main() {
    printf("\nmdec/mdec-glitch24c\n");
    printf("The 24bpp glitch: D cycles before each macroblock's last word 16, at full pace. No assertions.\n\n");
    for (int i = 0; i < 256; i++) input[i] = symbols_mdec[i * 4] | (symbols_mdec[i * 4 + 1] << 8) | (symbols_mdec[i * 4 + 2] << 16) | ((uint32_t)symbols_mdec[i * 4 + 3] << 24);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    runStep(600000, stepOut);
    static uint32_t PACE[15];
    for (int k = 0; k < 15; k++) PACE[k] = 128u * k;
    static uint32_t cpuOut[15][WORDS], dmaOut[2][WORDS], cpuTimes[15];
    static bool dmaOk[2];
    for (int k = 0; k < 15; k++) { runCpu(PACE[k], cpuOut[k]); cpuTimes[k] = cpuTime; }
    dmaOk[0] = runDma(32, dmaOut[0]);
    dmaOk[1] = runDma(16, dmaOut[1]);
    mdec_reset();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("STEP (mdec-step24's order, 600,000 cycles before each access): words 16 and 40 of each quadrant\n");
    for (int q = 0; q < WORDS / 48; q++)
        printf("  q%-2d  w16 %08x  w40 %08x   (w15 %08x  w17 %08x)\n", q, (unsigned)stepOut[q * 48 + 16], (unsigned)stepOut[q * 48 + 40],
               (unsigned)stepOut[q * 48 + 15], (unsigned)stepOut[q * 48 + 17]);
    char name[48];
    for (int k = 0; k < 15; k++) { for (int i = 0; i < WORDS; i++) out[i] = cpuOut[k][i]; sprintf(name, "D %u", (unsigned)PACE[k]); printf("(D %u: %u cycles for the 768 reads)\n", (unsigned)PACE[k], (unsigned)cpuTimes[k]); showDiff(name); }
    for (int k = 0; k < 2; k++) {
        /* DMA1 lays each macroblock out as its 16x16 raster (quadrant q's row r at raster row r or r + 8, left or
         * right half; 6 words an 8-pixel row at 24bpp, 12 a raster row): back into quadrant order for the comparison. */
        for (int mb = 0; mb < WORDS / 192; mb++)
            for (int q = 0; q < 4; q++)
                for (int w = 0; w < 48; w++) {
                    const int r = w / 6, c = w % 6, R = q < 2 ? r : r + 8;
                    out[mb * 192 + q * 48 + w] = dmaOut[k][mb * 192 + R * 12 + (q & 1) * 6 + c];
                }
        sprintf(name, "DMA1 %d%s", k ? 16 : 32, dmaOk[k] ? "" : " (DID NOT FINISH)");
        showDiff(name);
    }
    printf("\nmdec/mdec-glitch24c: Done.\n");
    for (;;) VSync(0);
}
