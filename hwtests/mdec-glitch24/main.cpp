/*
 * mdec/mdec-glitch24 - the 24bpp output word that reads the NEXT quadrant's half, and when.
 *
 * WHY THIS TEST EXISTS. hwtests/mdec-step24 (ps1-tests' step-by-step-log at 24bpp) on the SCPH-5501, 7501 and 9001,
 * two runs each, all six alike on all 768 output words: in every 48-word quadrant of the output, the LOW HALFWORD of
 * word 16 and of word 40 is not that quadrant's own - it is the low halfword of the same word of the NEXT quadrant
 * (10 of 10 quadrants where they differ at word 16, 9 of 9 at word 40, matched against this core's own decode). Those
 * are the two reads at which half a quadrant enters the output FIFO in the 24bpp status timing the same logs gave
 * (v0.377.37: the next quadrant lands at 9 words left, 33 less 24): the read meets the converter writing. That run read
 * the FIFO very slowly - the program printed every status line over Unirom's 115,200-baud cable, ~0.6M cycles a read -
 * and a race is a question of pace. Games read MDEC output by DMA1 (every 24-bit movie): whether they meet it there
 * decides whether this core must model it at all.
 *
 * WHAT IT DOES. The same four macroblocks (symbols.h, mdec-step24's), decoded at 24bpp nine ways, every output word
 * kept, printed only at the end:
 *   STEP     mdec-step24's own order - input by the CPU until the FIFO is full, output until it is empty, round and
 *            round - with 600,000 cycles before each access (the pace the consoles' printing gave it): the CONTROL,
 *            which must show the glitch where mdec-step24 did.
 *   CPU N    input by DMA0 all at once, output read by the CPU with N cycles before each read, N = 0, 64, 256, 1024,
 *            4096, 16384, 65536.
 *   DMA1 BS  input by DMA0, output by DMA1 in blocks of BS words, BS = 32 (what games use) and 16.
 * For STEP every quadrant's words 16 and 40 are printed; for every other way, each word that differs from STEP.
 *
 * READ IT LIKE THIS. A way that shows words 16 and 40 of a quadrant different from STEP - and equal to the quadrant's
 * own neighbours' colours - does NOT glitch at that pace; one identical to STEP does. If DMA1 reads glitch, games'
 * 24-bit movies carry it and the model needs it; if only slow CPU reads do, it is a curiosity of the step-by-step log.
 *
 * ON THIS CORE (v0.377.37, `node hwtests/run.js mdec-glitch24 --frames=3500 --quiet=3000`, ~25 s of the console's
 * time): no way glitches - every way's words (DMA1's put back in quadrant order) are
 * STEP's, and STEP's words 16 and 40 are each quadrant's own.
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
static void runCpu(uint32_t n, uint32_t* dst) {
    prepare();
    if (!cmdIdle()) return;
    write32(MDEC_CMD, CMD);
    startDma0();
    for (int o = 0; o < WORDS; o++) {
        wait(n);
        const uint32_t t0 = now();
        while (mdec_dataOutFifoEmpty()) if (now() - t0 > 33869000u) { dst[o] = 0xDEADDEAD; return; }   /* 1 s: stuck */
        dst[o] = read32(MDEC_DATA);
    }
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
    printf("\nmdec/mdec-glitch24\n");
    printf("The 24bpp word that reads the next quadrant's half, at every pace. No assertions.\n\n");
    for (int i = 0; i < 256; i++) input[i] = symbols_mdec[i * 4] | (symbols_mdec[i * 4 + 1] << 8) | (symbols_mdec[i * 4 + 2] << 16) | ((uint32_t)symbols_mdec[i * 4 + 3] << 24);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    runStep(600000, stepOut);
    static const uint32_t PACE[7] = { 0, 64, 256, 1024, 4096, 16384, 65536 };
    static uint32_t cpuOut[7][WORDS], dmaOut[2][WORDS];
    static bool dmaOk[2];
    for (int k = 0; k < 7; k++) runCpu(PACE[k], cpuOut[k]);
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
    for (int k = 0; k < 7; k++) { for (int i = 0; i < WORDS; i++) out[i] = cpuOut[k][i]; sprintf(name, "CPU %u", (unsigned)PACE[k]); showDiff(name); }
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
    printf("\nmdec/mdec-glitch24: Done.\n");
    for (;;) VSync(0);
}
