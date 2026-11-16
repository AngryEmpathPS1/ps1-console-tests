/*
 * mdec/mdec-slices0 - the console fuzzer's mdec-slices with column 0's DMA1 blocks stamped (each MADR step and DMA0's MADR beside it), to find
 * where a console's first column falls ~150 cycles behind this core (2026-11-16, the MDEC session; STATUS "mdecslices0").
 * Run 0 arms column 0's DMA1 at the game's cycle (~520 after the command), run 1 BEFORE the command: does a late DMA1 hold the first
 * output back (FF9's ~2,020 against mdec-time2's ~1,850)? B lines: each DMA1 MADR step's time FROM THE COMMAND, and DMA0's MADR.
 * mdec/mdec-slices - A GAME'S WHOLE MDEC COMMAND WITH ITS OWN DMA1 SCHEDULE: how far the decoder runs ahead between slices
 * (2026-11-15; the console fuzzer session, for the MDEC timing session: "replay its DMA1 slices with the game's own gaps - that is
 * exactly the run-ahead land_mdectime fitted").
 *
 * WHY. mdec-time2 found that while DMA1 is off the decoder banks only ~2 quadrants of output (this core banked a whole macroblock),
 * and Doom's STR player, restarting DMA1 for each of 20 columns a frame, read 24% short. A game's own command and schedule is the
 * case the fit was for, on the consoles beside the core.
 *
 * WHAT IT DOES. experiments/fuzz-all/mdecslices.js records one of a game's decode commands from a .pss (FF9's movie: 30002DA0h,
 * 11,680 words, 280 macroblocks at 24 bpp), the tables in force, and the game's DMA timetable from its command store: DMA0 (its
 * BCR as written: 365 blocks of 32), each DMA1 slice's CHCR-0 store, MADR (two buffers in turn), BCR (84 blocks of 32 - one
 * column of 14 macroblocks) and start. Here: the MDEC reset and 60000000h, the tables (command by the CPU, 32 words by DMA0), then
 * the decode command by the CPU, Timer 2 zeroed at that store, and every DMA store of the timetable at its own cycle (a busy-wait on
 * Timer 2; CPU work between them is not the game's - nothing else touches the bus). For each slice: the MDEC status just before its
 * start, its length (the CHCR start store to bit 24 clear, a ~10-cycle poll), and an FNV-1a of its words. DMA0's end is noted when
 * the waits see it. The whole command twice (the second a repeat).
 *   S <run> <k> at <cycle> stat <status before> len <cycles> sum <fnv>
 *   D <run> dma0end <cycle> | E <run> stat <status at the end>
 * No assertions. THE RULE: run it here first: `node tools/hwrunner.js <exe> --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

extern "C" { extern char _end[]; }

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MDEC0 0x1F801820u
#define MDEC1 0x1F801824u
#define D0_MADR 0x1F801080u
#define D0_BCR  0x1F801084u
#define D0_CHCR 0x1F801088u
#define D1_MADR 0x1F801090u
#define D1_BCR  0x1F801094u
#define D1_CHCR 0x1F801098u
#define DPCR    0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitDma(uint32_t chcr) { const uint32_t t0 = now(); while ((read32(chcr) & 0x01000000u) && now() - t0 < 8000000u) {} }
static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* the table (mdecslices.js): magic 'MSL1', words, quant[33], scale[33], cmd, nIn, in[nIn], d0bcr, nSlice, then per slice
 * { tStop, tStart, madrSel, bcr } (cycles from the command store; madrSel 0 / 1 picks a buffer) */
/* each slice its own region of fixed RAM (past the exe and its table, below the runner at 80130000h and up; not BSS), so no slice
 * waits on hashing: the sums are taken after the command (the first build hashed in the timetable and every start after the first
 * came ~55k cycles late). The game's two buffers in turn are an address only - the MDEC's timing does not see it. */
static uint32_t* const OUT = (uint32_t*)0x80040000u;

int main() {
    printf("\nmdec/mdec-slices\nA game's MDEC command with its own DMA1 timetable: per slice the status before it, its length, its words' sum.\n");
    const uint32_t* tb = (const uint32_t*)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 4096 && tb[0] != 0x314C534Du; i++) tb += 4;
    if (tb[0] != 0x314C534Du) { printf("no table\nmdec/mdec-slices0: Done.\n"); for (;;) VSync(0); }
    const uint32_t* q = tb + 2, *s = tb + 35, cmd = tb[68], nIn = tb[69];
    const uint32_t* in = tb + 70; const uint32_t d0bcr = in[nIn], nS = in[nIn + 1]; const uint32_t* sl = in + nIn + 2;
    printf("command %x, %u words in, DMA0 BCR %x, %u slices\n\n", (unsigned)cmd, (unsigned)nIn, (unsigned)d0bcr, (unsigned)nS);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000088u);   /* DMA0 and DMA1 on */
    static uint32_t rs[64], rl[64], rh[64], rt[64];
    static uint32_t bt[128], b1[128], b0[128]; uint32_t nb = 0;   /* mdec-slices0 */
    for (int run = 0; run < 2; run++) {
        write32(D0_CHCR, 0); write32(D1_CHCR, 0);
        write32(MDEC1, 0x80000000u);
        { lastT = read16(T2_COUNT); clk = 0; while (now() < 200) {} }
        write32(MDEC1, 0x60000000u);
        write32(MDEC0, q[0]); write32(D0_MADR, (uint32_t)(q + 1) & 0xFFFFFFu); write32(D0_BCR, 0x00010020u); write32(D0_CHCR, 0x01000201u); waitDma(D0_CHCR);
        write32(MDEC0, s[0]); write32(D0_MADR, (uint32_t)(s + 1) & 0xFFFFFFu); write32(D0_BCR, 0x00010020u); write32(D0_CHCR, 0x01000201u); waitDma(D0_CHCR);
        { lastT = read16(T2_COUNT); clk = 0; while (now() < 2000) {} }
        uint32_t d0end = 0;
        if (run == 1) {   /* mdec-slices0, run 1: column 0's DMA1 armed BEFORE the command (run 0: at the game's cycle) */
            write32(D1_CHCR, 0); write32(D1_MADR, (uint32_t)OUT & 0xFFFFFFu); write32(D1_BCR, sl[3]); write32(D1_CHCR, 0x01000200u);
        }
        write32(MDEC0, cmd);
        lastT = read16(T2_COUNT); clk = 0;
        while (now() < 12) {}
        write32(D0_MADR, (uint32_t)in & 0xFFFFFFu); write32(D0_BCR, d0bcr); write32(D0_CHCR, 0x01000201u);
        for (uint32_t k = 0; k < nS && k < 64; k++) {
            const uint32_t* e = sl + 4 * k;
            const bool early = (run == 1 && k == 0);
            if (!early) {
            while (now() < e[0]) { if (!d0end && !(read32(D0_CHCR) & 0x01000000u)) d0end = clk; }
            write32(D1_CHCR, 0);
            uint32_t* buf = OUT + 2688u * k;
            write32(D1_MADR, (uint32_t)buf & 0xFFFFFFu); write32(D1_BCR, e[3]);
            while (now() < e[1]) { if (!d0end && !(read32(D0_CHCR) & 0x01000000u)) d0end = clk; }
            }
            const uint32_t st = read32(MDEC1);
            const uint32_t t0 = early ? 0 : now();
            if (!early) write32(D1_CHCR, 0x01000200u);
            if (k == 0) {   /* mdec-slices0: column 0's DMA1 blocks stamped - each MADR step, and DMA0's MADR beside it */
                uint32_t m1 = read32(D1_MADR);
                while (read32(D1_CHCR) & 0x01000000u) {
                    const uint32_t x = read32(D1_MADR);
                    if (x != m1) { if (nb < 128) { bt[nb] = now();   /* from the command */ b1[nb] = x; b0[nb] = read32(D0_MADR); nb++; } m1 = x; }
                    if (now() - t0 > 8000000u) break;
                }
            } else
            while (read32(D1_CHCR) & 0x01000000u) { if (now() - t0 > 8000000u) break; }
            const uint32_t len = now() - t0;
            if (!d0end && !(read32(D0_CHCR) & 0x01000000u)) d0end = clk;
            rs[k] = st; rl[k] = len; rt[k] = t0;
        }
        for (uint32_t k = 0; k < nS && k < 64; k++) {
            const uint32_t n = (sl[4 * k + 3] >> 16) * (sl[4 * k + 3] & 0xFFFF), *buf = OUT + 2688u * k;
            uint32_t h = 2166136261u; for (uint32_t i = 0; i < n && i < 2688; i++) h = fnv(h, buf[i]); rh[k] = h;
        }
        const uint32_t endSt = read32(MDEC1);
        write32(D0_CHCR, 0); write32(D1_CHCR, 0);
        write32(MDEC1, 0x80000000u);
        ExitCriticalSection();
        for (uint32_t k = 0; k < nS && k < 64; k++) printf("S %d %u at %u stat %x len %u sum %x\n", run, (unsigned)k, (unsigned)rt[k], (unsigned)rs[k], (unsigned)rl[k], (unsigned)rh[k]);
        printf("D %d dma0end %u\nE %d stat %x\n", run, (unsigned)d0end, run, (unsigned)endSt);
        for (uint32_t i = 0; i < nb; i++) printf("B %d %u t %u m1 %x m0 %x\n", run, (unsigned)i, (unsigned)bt[i], (unsigned)b1[i], (unsigned)b0[i]);
        nb = 0;
        EnterCriticalSection();
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nmdec/mdec-slices0: Done.\n");
    for (;;) VSync(0);
}
