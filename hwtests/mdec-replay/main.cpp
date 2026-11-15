/*
 * mdec/mdec-replay - A GAME'S MDEC COMMAND WITH EVERY DMA AND GPU STORE AROUND IT, AT ITS OWN CYCLES (2026-11-15; the console fuzzer
 * session, for the MDEC timing session: "each finished slice DMA2'd to VRAM in the gap, as FF9 does, so DMA1's restart meets the GPU
 * upload's bus use the way the movie does"). mdec-slices' second exe.
 *
 * WHAT IT DOES. experiments/fuzz-all/mdecslices.js --gpu records, from a game's decode command store to its next one, every store the
 * game makes to DMA0 / DMA1 / DMA2's registers, DPCR, GP1(04h) and GP0 by the CPU (FF9's movie: per column, DMA1 84 x 32 words; then
 * GP1(04h) 0, GP0(01h), GP0(A0h) and its two words, GP1(04h) 2, DMA2 168 x 16 words from the column just decoded; DPCR 3B3BBBBBh
 * before each DMA1). Here: the tables (command by the CPU, 32 words by DMA0), the decode command by the CPU, Timer 2 zeroed at that
 * store, and every recorded store at its own cycle (a busy-wait on Timer 2 that also polls the three channels' CHCR for their ends;
 * MADRs mapped to this program's buffers - each DMA1 column its own, DMA2 the column the game sent). Not replayed: the CD's DMA3 in
 * the gaps (no disc), the CPU's own work between stores. Per DMA1 column: the MDEC status just before its start, its length (start
 * store to bit 24 clear, seen by the polls: a ~25-cycle quantum), an FNV-1a of its words; per DMA2 upload its length; DMA0's end.
 * The whole command twice.
 *   S <run> <k> at <cycle> stat <status before> len <cycles> sum <fnv>
 *   G <run> <k> at <cycle> len <cycles>
 *   D <run> dma0end <cycle> | E <run> stat <status at the end>
 * No assertions. THE RULE: run it here first: `node tools/hwrunner.js <exe> --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

extern "C" { extern char _end[]; }

#define IO(o) (0x1F801000u + (o))
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitDma(uint32_t chcr) { const uint32_t t0 = now(); while ((read32(chcr) & 0x01000000u) && now() - t0 < 8000000u) {} }
static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* the table (mdecslices.js --gpu): magic 'MRP1', words, quant[33], scale[33], cmd, nIn, in[nIn], nEv, then per store
 * { t (cycles from the command store), off | flags << 16, value }: off from 1F801000h; flag 1: the value is a DMA1 column's
 * index (its buffer here), flag 2: the input's address. The polls are this program's, not the game's: each I/O read may wait behind
 * a DMA block, the same on the consoles and the core - a confound common to both, not the game's own bus use */
static uint32_t* const OUT = (uint32_t*)0x80040000u;   /* past the exe and its table, below the runner (80130000h and up) */
/* THE STORES, RESOLVED BEFORE THE RUN (address, value, kind) and the first 64 in the scratchpad; the loop has no calls and no
 * spills. Two earlier builds issued them 150-250 cycles apart (io.h's read32 / write32 are calls, a dozen locals lived on the
 * stack, the table was read from RAM behind DMA0's burst): FF9's CHCR-0 / MADR / BCR / start, 44 cycles in the game, took ~600.
 * The scratchpad is off the bus; it is saved and put back. */
static volatile uint32_t* const SPAD = (volatile uint32_t*)0x1F800000u;
static uint32_t* const RES = (uint32_t*)0x80030000u;   /* 4 words a store, 2,048 stores at most */
static uint32_t spadSave[256];
enum { K_STORE = 0, K_D1 = 1, K_D2 = 2, K_D0 = 3, K_SKIP = 4 };
static uint32_t sAt[32], sSt[32], sLen[32], sSum[32], gAt[32], gLen[32];
static uint32_t nS, nG, d0end;
static void __attribute__((noinline)) replay(uint32_t nEv, uint32_t cmd) {
    volatile uint16_t* const T2 = (volatile uint16_t*)T2_COUNT;
    volatile uint32_t* const C0 = (volatile uint32_t*)IO(0x088), * const C1 = (volatile uint32_t*)IO(0x098), * const C2 = (volatile uint32_t*)IO(0x0A8);
    volatile uint32_t* const M1 = (volatile uint32_t*)IO(0x824);
    uint32_t c = 0, tPrev = 0, sOpen = 0xFFFFFFFFu, gOpen = 0xFFFFFFFFu, ns = 0, ng = 0, d0 = 0; bool d0on = false;
    *(volatile uint32_t*)IO(0x820) = cmd;
    uint16_t l = *T2;
    for (uint32_t i = 0; i < nEv; i++) {
        const uint32_t* e = i < 64 ? (const uint32_t*)SPAD + 4 * i : RES + 4 * i;
        const uint32_t t = e[0];
        if (i == 0 || t - tPrev >= 100) {   /* a store under 100 cycles behind the one before goes out at once */
            for (;;) {
                const uint16_t x = *T2; c += (uint16_t)(x - l); l = x;
                if (c >= t) break;
                if (d0on && !d0 && !(*C0 & 0x01000000u)) d0 = c;
                if (sOpen != 0xFFFFFFFFu && !(*C1 & 0x01000000u)) { sLen[sOpen] = c - sAt[sOpen]; sOpen = 0xFFFFFFFFu; }
                if (gOpen != 0xFFFFFFFFu && !(*C2 & 0x01000000u)) { gLen[gOpen] = c - gAt[gOpen]; gOpen = 0xFFFFFFFFu; }
            }
        }
        tPrev = t;
        volatile uint32_t* const a = (volatile uint32_t*)e[1]; const uint32_t v = e[2], k = e[3];
        if (k == K_STORE) { *a = v; continue; }
        if (k == K_SKIP) continue;
        { const uint16_t x = *T2; c += (uint16_t)(x - l); l = x; }
        if (k == K_D1 && ns < 32) { sSt[ns] = *M1; if (sOpen != 0xFFFFFFFFu) sLen[sOpen] = 0xFFFFFFFFu; sAt[ns] = c; *a = v; sOpen = ns++; continue; }
        if (k == K_D2 && ng < 32) { gAt[ng] = c; *a = v; gOpen = ng++; continue; }
        if (k == K_D0) d0on = true;
        *a = v;
    }
    for (uint32_t guard = 0; guard < 400000u && ((*C1 | *C2) & 0x01000000u); guard++) {
        const uint16_t x = *T2; c += (uint16_t)(x - l); l = x;
        if (sOpen != 0xFFFFFFFFu && !(*C1 & 0x01000000u)) { sLen[sOpen] = c - sAt[sOpen]; sOpen = 0xFFFFFFFFu; }
        if (gOpen != 0xFFFFFFFFu && !(*C2 & 0x01000000u)) { gLen[gOpen] = c - gAt[gOpen]; gOpen = 0xFFFFFFFFu; }
    }
    if (d0on && !d0) { for (uint32_t guard = 0; guard < 400000u && (*C0 & 0x01000000u); guard++) {} const uint16_t x = *T2; c += (uint16_t)(x - l); d0 = c; }
    nS = ns; nG = ng; d0end = d0;
}
#define COL 2688u

int main() {
    printf("\nmdec/mdec-replay\nA game's MDEC command with its DMA and GPU stores at their own cycles: per DMA1 column the status before it,\nits length and its words' sum; per DMA2 upload its length.\n");
    const uint32_t* tb = (const uint32_t*)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 8192 && tb[0] != 0x3150524Du; i++) tb += 4;
    if (tb[0] != 0x3150524Du) { printf("no table\nmdec/mdec-replay: Done.\n"); for (;;) VSync(0); }
    const uint32_t* q = tb + 2, *s = tb + 35, cmd = tb[68], nIn = tb[69];
    const uint32_t* in = tb + 70; const uint32_t nEv = in[nIn]; const uint32_t* ev = in + nIn + 1;
    printf("command %x, %u words in, %u stores\n\n", (unsigned)cmd, (unsigned)nIn, (unsigned)nEv);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dpcr0 = read32(IO(0x0F0)), dicr0 = read32(IO(0x0F4));
    write16(T2_MODE, 0);
    for (int i = 0; i < 256; i++) spadSave[i] = SPAD[i];
    for (int run = 0; run < 2; run++) {
        write32(IO(0x0F0), dpcr0 | 0x00000888u);   /* DMA0, 1 and 2 on (the game's own DPCR follows from its stores) */
        write32(IO(0x088), 0); write32(IO(0x098), 0); write32(IO(0x0A8), 0);
        write32(IO(0x814), 0x01000000u); write32(IO(0x814), 0x04000000u); write32(IO(0x810), 0xE6000000u);
        write32(IO(0x824), 0x80000000u);
        { lastT = read16(T2_COUNT); clk = 0; while (now() < 200) {} }
        write32(IO(0x824), 0x60000000u);
        write32(IO(0x820), q[0]); write32(IO(0x080), (uint32_t)(q + 1) & 0xFFFFFFu); write32(IO(0x084), 0x00010020u); write32(IO(0x088), 0x01000201u); waitDma(IO(0x088));
        write32(IO(0x820), s[0]); write32(IO(0x080), (uint32_t)(s + 1) & 0xFFFFFFu); write32(IO(0x084), 0x00010020u); write32(IO(0x088), 0x01000201u); waitDma(IO(0x088));
        { lastT = read16(T2_COUNT); clk = 0; while (now() < 2000) {} }
        for (uint32_t i = 0; i < nEv && i < 2048; i++) {
            const uint32_t off = ev[3 * i + 1] & 0xFFFF, fl = ev[3 * i + 1] >> 16; uint32_t v = ev[3 * i + 2], k = K_STORE;
            if (fl & 1) v = (uint32_t)(OUT + COL * (v & 31)) & 0xFFFFFFu;
            if (fl & 2) v = (uint32_t)in & 0xFFFFFFu;
            if (off == 0x098 && (v & 0x01000000u)) k = K_D1;
            if (off == 0x0A8 && (v & 0x01000000u)) k = ((v >> 9) & 3) == 2 ? K_SKIP : K_D2;   /* never a linked list */
            if (off == 0x088 && (v & 0x01000000u)) k = K_D0;
            if (off == 0x0F4) v &= 0x00FFFFFFu;   /* DICR without its flags: interrupts are off */
            RES[4 * i] = ev[3 * i]; RES[4 * i + 1] = IO(off); RES[4 * i + 2] = v; RES[4 * i + 3] = k;
        }
        for (uint32_t i = 0; i < 256 && i < 4 * nEv; i++) SPAD[i] = RES[i];
        replay(nEv < 2048 ? nEv : 2048, cmd);
        const uint32_t endSt = read32(IO(0x824));
        write32(IO(0x088), 0); write32(IO(0x098), 0); write32(IO(0x0A8), 0);
        write32(IO(0x814), 0x04000000u);
        write32(IO(0x824), 0x80000000u);
        write32(IO(0x0F0), dpcr0); write32(IO(0x0F4), dicr0 & 0x00FFFFFFu);
        for (uint32_t k = 0; k < nS; k++) { uint32_t h = 2166136261u; for (uint32_t j = 0; j < COL; j++) h = fnv(h, OUT[COL * k + j]); sSum[k] = h; }
        ExitCriticalSection();
        for (uint32_t k = 0; k < nS; k++) printf("S %d %u at %u stat %x len %u sum %x\n", run, (unsigned)k, (unsigned)sAt[k], (unsigned)sSt[k], (unsigned)sLen[k], (unsigned)sSum[k]);
        for (uint32_t k = 0; k < nG; k++) printf("G %d %u at %u len %u\n", run, (unsigned)k, (unsigned)gAt[k], (unsigned)gLen[k]);
        printf("D %d dma0end %u\nE %d stat %x\n", run, (unsigned)d0end, run, (unsigned)endSt);
        EnterCriticalSection();
    }
    write16(T2_MODE, m2);
    for (int i = 0; i < 256; i++) SPAD[i] = spadSave[i];
    ExitCriticalSection();
    printf("\nmdec/mdec-replay: Done.\n");
    for (;;) VSync(0);
}
