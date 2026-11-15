/*
 * spu/spu-replay - A GAME'S OWN DMA4 SOUND-RAM WRITE, ITS STORES AROUND IT AT THEIR CYCLES (2026-11-15; the console fuzzer session, for
 * the SPU session: "the transfer's time, CHCR write to busy clear, and the time to SPUSTAT bit 10 clearing after it ... what the game
 * does next and when ... the write, then the cycle I_STAT bit 9 rises").
 *
 * WHY. On six consoles a 16-word DMA4 block comes every 1,108.9 cycles at 1F801014h = 200931E1h and v0.377.355 gives 1,075.9 (the SPU
 * session's measurement); a game's 64-block transfer should read ~2,100 cycles longer on the consoles. And the write FIFO's tail (up
 * to 17 halfwords) may not have landed when a game switches the transfer mode soon after the DMA's end.
 *
 * WHAT IT DOES. experiments/fuzz-all/spuseed.js records one of a game's DMA4 writes from a .pss (Metal Gear Solid: 1,024 words, 16 x 64,
 * into sound RAM EA02h x 8 at 200931E1h): its stores from the transfer address write to the CHCR start (the address, its read back,
 * SPUCNT read / C0E0h / read, MADR, BCR, CHCR - at the game's cycles from the CHCR store), its own 1F801014h, and the game's stores
 * after the transfer (SPUCNT read, C0C0h, read) with their delay from the DMA's end in the core. Here, per case:
 *   - the target region filled with A5A5h (DMA4 at 220931E1h, its FIFO drained) and voices off;
 *   - 1F801014h the game's; the pre-stores at their cycles; Timer 2 from the CHCR store: CHCR bit 24 clear (end), then SPUSTAT bit 10
 *     clear (busy), polled tightly;
 *   - the game's post-stores at END + d, d = the game's own delay and a sweep (0, 50, 200, 500, 1000, 2000 cycles);
 *   - 20,000 cycles, the region read back at 220931E1h: words that hold the game's data, words still A5A5h, others; a sum a 256-byte row.
 *   And an IRQ kind: the SPU IRQ address set inside the target (word 16, 512, 1008 of the transfer), SPUCNT's IRQ bit on, I_STAT bit 9
 *   polled through the transfer - the cycle the write reaches that address.
 *   C <id> d <d> end <cycles> busy <cycles> landed <n> marker <n> other <n> sums ...
 *   Q <id> irq@<word> at <cycles> end <cycles>
 * No assertions. THE RULE: run it here first: `node tools/hwrunner.js <exe> --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

extern "C" { extern char _end[]; }

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define SPU(o) R16(0x1F801C00u + (o))
#define T2 R16(0x1F801120u)
#define I_STAT R32(0x1F801070u)
#define D4_MADR R32(0x1F8010C0u)
#define D4_BCR  R32(0x1F8010C4u)
#define D4_CHCR R32(0x1F8010C8u)
#define SPUDELAY R32(0x1F801014u)

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = T2; clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static uint16_t spucnt;
static void spuMode(int m) { spucnt = (uint16_t)((spucnt & ~0x30) | (m << 4)); SPU(0x1AA) = spucnt; for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x3F) != (spucnt & 0x3F); i++) {} }
/* as hwtests/fuzz-all: a sound RAM read at 220931E1h (the boot value's reads are unstable), the value put back after */
static void spuDma(uint32_t spuAddr, uint32_t* ram, uint32_t words, bool toSpu) {
    const uint32_t d0 = SPUDELAY;
    SPUDELAY = 0x220931E1u;
    spuMode(0); SPU(0x1A6) = (uint16_t)(spuAddr / 8); spuMode(toSpu ? 2 : 3);
    D4_MADR = (uint32_t)ram & 0xFFFFFF; D4_BCR = ((words / 16) << 16) | 16; D4_CHCR = toSpu ? 0x01000201u : 0x01000200u;
    for (int i = 0; i < 2000000 && (D4_CHCR & 0x01000000u); i++) {}
    for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x0400); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
    SPUDELAY = d0;
}
static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

/* the table (spuseed.js): 'SPR1', words, delay (1F801014h), spuAddr (bytes), nData, data[nData], nPre, pre[nPre] { t from the CHCR
 * store (signed), off (from 1F801000h) | kind << 16 (0 store, 1 read, 2 MADR = the data), value }, nPost, post[nPost] { t from the
 * DMA's end, off | kind << 16, value }, the game's post delay in the core */
static uint32_t* const MARKBUF = (uint32_t*)0x80060000u;
static uint32_t* const READBUF = (uint32_t*)0x80070000u;

struct Res { uint32_t end, busy, landed, marker, other; uint32_t sum[16]; };
static Res res;
static void runCase(const uint32_t* tb, uint32_t d, int irqWord, uint32_t* irqAt) {
    const uint32_t delay = tb[2], addr = tb[3], nData = tb[4]; const uint32_t* data = tb + 5;
    const uint32_t nPre = data[nData]; const uint32_t* pre = data + nData + 1; const uint32_t nPost = pre[3 * nPre]; const uint32_t* post = pre + 3 * nPre + 1;
    /* the region to the marker, voices off */
    for (uint32_t i = 0; i < nData; i++) MARKBUF[i] = 0xA5A5A5A5u;
    SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
    /* the reverb's work area kept out of the region: MGS's SPUCNT (C0C0h / C0E0h) has the reverb master on, and at the reverb base
     * the run found (38000h, to the top of sound RAM) each sample wrote into 75010h - the SPU session found it (the core with no
     * --lock, 78-188 stray words in pass 2; the 7001's 9-113 the same mechanism). FFFEh puts it at the last 16 bytes. */
    SPU(0x1A2) = 0xFFFE;
    spucnt = 0xC000; SPU(0x1AA) = spucnt; idle(2000);
    spuDma(addr, MARKBUF, nData, true);
    if (irqWord >= 0) { SPU(0x1A4) = (uint16_t)((addr + 4 * (uint32_t)irqWord) / 8); }
    /* the game's SPUCNT mode at the start (its pre-stores move it), its delay */
    spucnt = (uint16_t)(irqWord >= 0 ? 0xC0C0 : 0xC080); SPU(0x1AA) = spucnt; idle(2000);
    I_STAT = ~0x200u;
    const uint32_t dly0 = SPUDELAY; SPUDELAY = delay;
    /* the pre-stores: t is from the CHCR store, the first negative - the clock starts 1,000 cycles before it */
    lastT = T2; clk = 0;
    for (uint32_t i = 0; i < nPre; i++) {
        const uint32_t t = pre[3 * i] + 1000u, off = pre[3 * i + 1] & 0xFFFF, k = pre[3 * i + 1] >> 16, v = pre[3 * i + 2];
        while (now() < t) {}
        if (k == 1) { (void)R16(0x1F801000u + off); continue; }
        if (k == 2) { D4_MADR = (uint32_t)data & 0xFFFFFF; continue; }
        if (off >= 0xC00) { R16(0x1F801000u + off) = (uint16_t)v; if (off == 0xDAA) spucnt = (uint16_t)v; }
        else R32(0x1F801000u + off) = v;
    }
    const uint32_t t0 = now();
    uint32_t end = 0, busy = 0, irq = 0;
    for (uint32_t g = 0; g < 4000000u; g++) { const uint32_t c = now(); if (!irq && (I_STAT & 0x200u)) irq = c; if (!(D4_CHCR & 0x01000000u)) { end = c; break; } }
    for (uint32_t g = 0; g < 4000000u; g++) { const uint32_t c = now(); if (!irq && (I_STAT & 0x200u)) irq = c; if (!(SPU(0x1AE) & 0x0400)) { busy = c; break; } }
    /* the game's post-stores at END + d */
    for (uint32_t i = 0; i < nPost; i++) {
        const uint32_t off = post[3 * i + 1] & 0xFFFF, k = post[3 * i + 1] >> 16, v = post[3 * i + 2];
        const uint32_t t = end + d + (post[3 * i] - post[0]);
        while (now() < t) { if (!irq && (I_STAT & 0x200u)) irq = now(); }
        if (k == 1) { (void)R16(0x1F801000u + off); continue; }
        R16(0x1F801000u + off) = (uint16_t)v; if (off == 0xDAA) spucnt = (uint16_t)v;
    }
    { const uint32_t ta = now(); while (now() - ta < 20000u) { if (!irq && (I_STAT & 0x200u)) irq = now(); } }
    SPUDELAY = dly0;
    if (irqAt) *irqAt = irq ? irq - t0 : 0;
    I_STAT = ~0x200u;
    spucnt = 0xC000; SPU(0x1AA) = spucnt; idle(1000);
    spuDma(addr, READBUF, nData, false);
    res.end = end - t0; res.busy = busy - t0; res.landed = res.marker = res.other = 0;
    for (uint32_t r = 0; r < 16; r++) res.sum[r] = 2166136261u;
    for (uint32_t i = 0; i < nData; i++) {
        const uint32_t b = READBUF[i];
        if (b == data[i]) res.landed++; else if (b == 0xA5A5A5A5u) res.marker++; else res.other++;
        res.sum[(i * 16) / nData] = fnv(res.sum[(i * 16) / nData], b);
    }
}

int main() {
    printf("\nspu/spu-replay\nA game's DMA4 write with its stores at their cycles: the end, SPUSTAT bit 10, what landed, the IRQ's cycle.\n");
    const uint32_t* tb = (const uint32_t*)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 8192 && tb[0] != 0x31525053u; i++) tb += 4;
    if (tb[0] != 0x31525053u) { printf("no table\nspu/spu-replay: Done.\n"); for (;;) VSync(0); }
    const uint32_t nData = tb[4]; const uint32_t* data = tb + 5; const uint32_t nPre = data[nData]; const uint32_t* pre = data + nData + 1;
    const uint32_t nPost = pre[3 * nPre]; const uint32_t* post = pre + 3 * nPre + 1; const uint32_t gameD = post[3 * nPost];
    printf("delay %x, sound RAM %x, %u words, %u stores before, %u after (the game's %u cycles after the end)\n\n", (unsigned)tb[2], (unsigned)tb[3], (unsigned)nData, (unsigned)nPre, (unsigned)nPost, (unsigned)gameD);
    EnterCriticalSection();
    const uint16_t m2 = R16(0x1F801124u), cnt0 = SPU(0x1AA);
    const uint32_t mask0 = R32(0x1F801074u), dpcr0 = R32(0x1F8010F0u);
    R32(0x1F8010F0u) = dpcr0 | 0x00080000u;   /* DMA4 on: the runner leaves its enable bit clear (hwtests/fuzz-all sets it the same way) */
    R32(0x1F801074u) = 0;
    R16(0x1F801124u) = 0;
    const uint16_t mv0 = SPU(0x180), mv1 = SPU(0x182), rb0 = SPU(0x1A2);
    SPU(0x180) = 0; SPU(0x182) = 0;
    const uint32_t ds[7] = { gameD, 0, 50, 200, 500, 1000, 2000 };
    for (int rep = 0; rep < 2; rep++)
        for (int k = 0; k < 7; k++) {
            runCase(tb, ds[k], -1, 0);
            ExitCriticalSection();
            printf("C %d d %u end %u busy %u landed %u marker %u other %u sums", rep * 7 + k, (unsigned)ds[k], (unsigned)res.end, (unsigned)res.busy, (unsigned)res.landed, (unsigned)res.marker, (unsigned)res.other);
            for (int r = 0; r < 16; r++) printf(" %x", (unsigned)res.sum[r]);
            printf("\n");
            EnterCriticalSection();
        }
    const int irqW[3] = { 16, 512, 1008 };
    for (int rep = 0; rep < 2; rep++)
        for (int k = 0; k < 3; k++) {
            uint32_t at = 0;
            runCase(tb, gameD, irqW[k], &at);
            ExitCriticalSection();
            printf("Q %d irq@%d at %u end %u\n", rep * 3 + k, irqW[k], (unsigned)at, (unsigned)res.end);
            EnterCriticalSection();
        }
    SPU(0x1A4) = 0; SPU(0x1A2) = rb0; SPU(0x180) = mv0; SPU(0x182) = mv1; SPU(0x1AA) = cnt0;
    I_STAT = ~0x200u; R32(0x1F801074u) = mask0; R32(0x1F8010F0u) = dpcr0;
    R16(0x1F801124u) = m2;
    ExitCriticalSection();
    printf("\nspu/spu-replay: Done.\n");
    for (;;) VSync(0);
}
