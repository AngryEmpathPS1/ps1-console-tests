/*
 * fuzz/fuzz-all - THE WHOLE-CONSOLE FUZZER'S HOST (2026-11-13). Not a test of its own: tools/fuzzall.js writes a batch of
 * random cases (MIPS code that touches the CPU, GTE, GPU, MDEC, SPU, DMA, timers and IRQs) and appends them to this
 * program's image as a table; this program runs each case cold and warm, timed to the cycle, and prints what it can read
 * back. The same exe runs on the consoles and on the emulator, and tools/fuzzall.js diffs the three.
 *
 * WHY. 2026-11-12/13 the main session ran seven hand-written two-second tests (hwtests/icache-place*) on the second
 * SCPH-5501 and the PSone; the two agreed byte for byte every time and every one found something. This does that with
 * no hands, across the whole console and all of it at once (the plan: memory note "console fuzz", 2026-11-01).
 *
 * EACH CASE. Its state is rebuilt before each of its two runs (wrap.s fz_one): 2 KB of DATA and the scratchpad from a
 * seeded copy, the GTE's 64 registers, HI/LO and the case's 20 GPRs from the seed, the MDEC reset, GP1(01h), the timers,
 * I_STAT/I_MASK, DPCR/DICR as found. Before the cold run C sets the rest: the GPU's drawing environment (draw area
 * (0,256)-(127,319), offset (0,256), texture page (128,256)) and the draw area filled; the SPU's voices released and
 * keyed off, sound RAM 1000h-1FFFh rewritten from the seed; then the I-cache flushed. After both runs the draw area is
 * read back through GPUREAD and sound RAM 0-1FFFh by DMA.
 *
 * THE LINE. "c id tCold tWarm gprC gprW memC memW gteC gteW vram spuData spuCap" - times in cycles (Timer 2, the start
 * aligned on the refresh grid: wrap.s), the rest hashes (FNV-1a of words). The table's verbose flag adds every value
 * (tools/fuzzall.js's shrinker and trail read those).
 *
 * SAFE. No CD access, no memory-control or cache-control writes, no SIO, no Timer 2 writes, interrupts off throughout;
 * the generator keeps every load and store inside DATA, the scratchpad and a list of I/O registers (tools/fuzzall.js).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

extern "C" {
void fz_pair(void);
extern uint32_t fz_wrap_start[], fz_wrap_end[];
extern char _end[];
void fz_flush(void);
uint32_t fz_getsr(void);
void fz_setsr(uint32_t);
}
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".global fz_flush\nfz_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n"
    ".global fz_getsr\nfz_getsr:\n  mfc0 $v0, $12\n  jr $ra\n  nop\n"
    ".global fz_setsr\nfz_setsr:\n  mtc0 $a0, $12\n  nop\n  jr $ra\n  nop\n"
    ".set pop\n");

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define GP0 R32(0x1F801810u)
#define GPUREAD R32(0x1F801810u)
#define GP1 R32(0x1F801814u)
#define GPUSTAT R32(0x1F801814u)
#define I_STAT R32(0x1F801070u)
#define I_MASK R32(0x1F801074u)
#define DPCR R32(0x1F8010F0u)
#define DICR R32(0x1F8010F4u)
#define MDEC1 R32(0x1F801824u)
#define SPU(o) R16(0x1F801C00u + (o))
#define T2 R16(0x1F801120u)

#define CODEBUF  0x80100000u
#define DATA     0x80102000u
#define DATAINIT ((uint32_t *)0x80102800u)
#define SCRINIT  ((uint32_t *)0x80103000u)
#define CTX      ((uint32_t *)0x80103400u)
#define OUT0     ((uint32_t *)0x80104000u)
#define OUT1     ((uint32_t *)0x80105000u)
#define SPUBUF   ((uint32_t *)0x80106000u)
#define TEXBUF   ((uint32_t *)0x80108000u)
#define TABLE    ((uint32_t *)0x80110000u)
#define TABLE_MAX ((0x8012F000u - 0x80110000u) / 4)
#define SCRSAVE  ((uint32_t *)0x8012F000u)

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
/* a register's starting value: zero, small, a sign edge or anything */
static uint32_t rval() {
    const uint32_t k = rnd() & 15;
    if (k == 0) return 0;
    if (k < 4) return rnd() & 31;
    if (k == 4) return 0x7FFFFFFFu + (rnd() & 3);
    if (k == 5) return 0xFFFFFFFFu - (rnd() & 15);
    if (k == 6) return rnd() & 0xFFFF;
    return rnd();
}
#define FNV0 2166136261u
static inline uint32_t fnv(uint32_t h, uint32_t w) { return (h ^ w) * 16777619u; }
static uint32_t hashw(const uint32_t *p, uint32_t n, uint32_t h = FNV0) { for (uint32_t i = 0; i < n; i++) h = fnv(h, p[i]); return h; }

static void gpuWait(uint32_t bit) { for (int i = 0; i < 400000 && !(GPUSTAT & (1u << bit)); i++) {} }
static void dmaWait(uint32_t ch) { for (int i = 0; i < 2000000 && (R32(0x1F801088u + ch * 16) & 0x01000000u); i++) {} }
/* Timer 2 wraps at 49,152 from main() on (its target, below), so a step across the wrap is t + 49152 - l: as a 16-bit difference it
 * read 16,384 cycles more and ended the wait at once - ~8 % of the 4,000-cycle waits after a sound RAM refill cut short, the write's
 * tail lost, at the same cases on every console (CPU time decides the wrap) but at others in the emulator (the SPU session,
 * 2026-11-13: batch-600's 26 / 53 / 55). A free-running Timer 2 (before main sets the target) wraps at 65,536: counted short here,
 * so a wait is only ever longer. */
static void idle(uint32_t c) { uint32_t s = 0; uint16_t l = T2; while (s < c) { const uint16_t t = T2; s += t >= l ? (uint32_t)(t - l) : (uint32_t)t + 49152u - l; l = t; } }

static uint16_t spucnt;
static void spuMode(int m) {
    spucnt = (uint16_t)((spucnt & ~0x30) | (m << 4));
    SPU(0x1AA) = spucnt;
    for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x3F) != (spucnt & 0x3F); i++) {}
}
/* A sound RAM READ runs at SPU delay 220931E1h (1F801014h), put back after: at the boot value the console's read is unstable -
 * stray FFFFh pairs every 9 blocks on the second 5501, every 2 on the 7502 (the SPU session, 2026-11-13, hwtests/fuzz-spu sets
 * the same; exact on all three consoles with it) - and the night's spuData findings were that, not the emulator. Only around the
 * readback, so the cases themselves still run at the console's own delay. */
static void spuDma(uint32_t spuAddr, uint32_t *ram, uint32_t words, bool toSpu) {
    const uint32_t spuDelay = R32(0x1F801014u);
    if (!toSpu) R32(0x1F801014u) = 0x220931E1u;
    spuMode(0);
    SPU(0x1A6) = (uint16_t)(spuAddr / 8);
    spuMode(toSpu ? 2 : 3);
    R32(0x1F8010C0u) = (uint32_t)ram & 0xFFFFFF;
    R32(0x1F8010C4u) = ((words / 16) << 16) | 16;
    R32(0x1F8010C8u) = toSpu ? 0x01000201u : 0x01000200u;
    dmaWait(4);
    for (int i = 0; i < 100000 && (SPU(0x1AE) & 0x0400); i++) {}
    if (toSpu) idle(4000);                     /* the SPU still writing its FIFO out (hwtests/fuzz-spu waits the same) */
    spuMode(0);
    R32(0x1F801014u) = spuDelay;
}
static void gpuDmaWords(const uint32_t *p, uint32_t words) {
    GP1 = 0x04000002u;
    R32(0x1F8010A0u) = (uint32_t)p & 0xFFFFFF;
    R32(0x1F8010A4u) = ((words / 16) << 16) | 16;
    R32(0x1F8010A8u) = 0x01000201u;
    dmaWait(2);
    GP1 = 0x04000000u;
}

static uint32_t codeOff;
static uint32_t dpcr0, dicr0;

/* per case, before the cold run: the parts the wrapper does not rebuild */
static void setupCase(uint32_t seed) {
    /* the GPU: idle, then the drawing environment, then the draw area filled */
    gpuWait(26); gpuWait(28);
    GP0 = 0xE1000012u;                         /* texture page (128,256), 4-bit, no dither */
    GP0 = 0xE2000000u;
    GP0 = 0xE3000000u | (256u << 10);          /* draw area (0,256) */
    GP0 = 0xE4000000u | (319u << 10) | 127u;   /* to (127,319) */
    GP0 = 0xE5000000u | (256u << 11);          /* offset (0,256) */
    GP0 = 0xE6000000u;
    gpuWait(26);
    GP0 = 0x02000000u | (rnd() & 0xFFFFFF);
    (void)seed;
    GP0 = 256u << 16;
    GP0 = (64u << 16) | 128u;
    gpuWait(26);
    /* the SPU: every voice released at once and keyed off, its registers from the seed, sound RAM 1000h-1FFFh refilled */
    for (uint32_t v = 0; v < 24; v++) SPU(v * 16 + 0xA) = 0x0000;
    /* ENDX holds until a key-on: every voice keyed on at 2000h (zeros since the runner cleared sound RAM, no end flag in
     * reach), so it reads 0 again, then released at once */
    for (uint32_t v = 0; v < 24; v++) { SPU(v * 16 + 0x6) = 0x2000 / 8; SPU(v * 16 + 0x4) = 0x1000; SPU(v * 16 + 0x8) = 0x000F; }
    SPU(0x188) = 0xFFFF; SPU(0x18A) = 0x00FF;
    idle(768u * 4u);
    SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
    idle(768u * 8u);
    SPU(0x18C) = 0; SPU(0x18E) = 0;
    SPU(0x188) = 0; SPU(0x18A) = 0;
    for (uint32_t r = 0x190; r < 0x19C; r += 2) SPU(r) = 0;
    for (uint32_t v = 0; v < 24; v++) {
        SPU(v * 16 + 0x0) = (uint16_t)(rnd() & 0x3FFF);
        SPU(v * 16 + 0x2) = (uint16_t)(rnd() & 0x3FFF);
        SPU(v * 16 + 0x4) = (uint16_t)(rnd() & 0x3FFF);
        SPU(v * 16 + 0x6) = (uint16_t)(0x200 + (rnd() & 0x1FF));
        SPU(v * 16 + 0x8) = (uint16_t)rnd();
        SPU(v * 16 + 0xA) = (uint16_t)rnd();
        SPU(v * 16 + 0xE) = (uint16_t)(0x200 + (rnd() & 0x1FF));
    }
    spucnt = 0xC000; SPU(0x1AA) = spucnt;
    for (uint32_t i = 0; i < 1024; i++) SPUBUF[i] = rnd();
    spuDma(0x1000, SPUBUF, 1024, true);
    MDEC1 = 0x80000000u;
    idle(200000);                              /* the SPU's transfer and the GPU's fill done before the cold run */
}

static bool dumpVram;
static uint32_t vramHash(bool verbose, uint32_t id) {
    gpuWait(26); gpuWait(28);
    GP0 = 0x01000000u;
    GP0 = 0xC0000000u;
    GP0 = 256u << 16;
    GP0 = (64u << 16) | 128u;
    gpuWait(27);
    uint32_t h = FNV0, rows[64];
    for (uint32_t y = 0; y < 64; y++) {
        uint32_t r = FNV0;
        if (dumpVram) printf("VW %u %u", id, y);   /* flags bit 3: every word of the draw area (a VRAM dump, 64 lines) */
        for (uint32_t x = 0; x < 64; x++) { const uint32_t w = GPUREAD; h = fnv(h, w); r = fnv(r, w); if (dumpVram) printf(" %x", w); }
        if (dumpVram) printf("%c", 10);
        rows[y] = r;
    }
    GP0 = 0xC0000000u; GP0 = (256u << 16) | 128u; GP0 = (1u << 16) | 2u;   /* GPUREAD left on a word of the fixed texture region */
    gpuWait(27);
    (void)GPUREAD;
    if (verbose) { printf("VR %u", id); for (int y = 0; y < 64; y++) printf(" %x", rows[y]); printf("\n"); }
    return h;
}

static bool regsOnly;
static void verboseRun(uint32_t id, int run, const uint32_t *o) {
    const bool vgOnly = run < 0;
    if (vgOnly) run = -run - 1;
    static const uint8_t regs[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 21, 22, 23, 24, 25 };
    printf("VG %u %d %u", id, run, o[0]);
    for (uint32_t i = 0; i < sizeof regs; i++) printf(" %x", o[regs[i]]);
    printf(" %x %x %x\n", o[33], o[34], o[99]);
    if (vgOnly && regsOnly) return;   /* flags bit 2: the registers alone, for probes of 350 cases */
    printf("VX %u %d", id, run);
    for (int i = 0; i < 64; i++) printf(" %x", o[35 + i]);
    if (vgOnly) { printf("\n"); return; }   /* lite: registers and the GTE's */
    printf("\nVD %u %d", id, run);
    for (int i = 0; i < 512; i++) if (o[128 + i] != DATAINIT[i]) printf(" %x:%x", i * 4, o[128 + i]);
    printf("\nVS %u %d", id, run);
    for (int i = 0; i < 256; i++) if (o[640 + i] != SCRINIT[i]) printf(" %x:%x", i * 4, o[640 + i]);
    printf("\n");
}

static uint32_t gprHash(const uint32_t *o) {
    static const uint8_t regs[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 21, 22, 23, 24, 25, 33, 34, 99 };
    uint32_t h = FNV0;
    for (uint32_t i = 0; i < sizeof regs; i++) h = fnv(h, o[regs[i]]);
    return h;
}

/* the breadcrumb: RAM survives a RESET (STATUS line 38652), so a console that hangs in a case can say which through Unirom's
 * memory dump - BREAD = the case's id, BREAD+4 = 1 in the timed runs, 2 in the readback, 0 between cases */
#define BREAD ((volatile uint32_t *)0x8012FC00u)
/* flags bit 1 (lite): only the VG lines (time and registers), for probes of a few hundred cases inside the runner's 256 KB log */
static bool lite;
static void runCase(const uint32_t *c, bool verbose) {
    const uint32_t id = c[0], seed = c[1], nCode = c[2], nOver = c[3];
    BREAD[0] = id; BREAD[1] = 1;
    const uint32_t *code = c + 4, *over = c + 4 + nCode;
    rng = seed * 0x9E3779B9u + 0x1234567u; if (!rng) rng = 1;
    for (int i = 0; i < 8; i++) rnd();
    for (int i = 0; i < 512; i++) DATAINIT[i] = rval();
    for (int i = 0; i < 256; i++) SCRINIT[i] = rval();
    for (int r = 0; r < 32; r++) CTX[8 + r] = rval();
    CTX[40] = rval(); CTX[41] = rval();
    for (int i = 0; i < 64; i++) CTX[48 + i] = rval();
    for (uint32_t i = 0; i < nOver; i++) {
        const uint32_t k = over[2 * i], v = over[2 * i + 1], t = k >> 28, n = k & 0xFFFF;
        if (t == 0 && n < 512) DATAINIT[n] = v;
        else if (t == 1 && n < 256) SCRINIT[n] = v;
        else if (t == 2 && n < 32) CTX[8 + n] = v;
        else if (t == 3 && n < 64) CTX[48 + n] = v;
        else if (t == 4 && n < 2) CTX[40 + n] = v;
    }
    volatile uint32_t *dst = (volatile uint32_t *)(CODEBUF + codeOff);
    for (uint32_t i = 0; i < nCode; i++) dst[i] = code[i];
    dst[nCode] = 0x03E00008u;                  /* jr ra */
    dst[nCode + 1] = 0;
    CTX[0] = CODEBUF + codeOff;
    setupCase(seed);
    fz_flush();
    fz_setsr((fz_getsr() & ~1u) | 0x40000000u);
    I_MASK = 0;
    fz_pair();
    BREAD[1] = 2;
    I_MASK = 0; I_STAT = 0;
    GP1 = 0x01000000u; GP1 = 0x04000000u;
    if (verbose) { verboseRun(id, 0, OUT0); verboseRun(id, 1, OUT1); }
    else if (lite) { verboseRun(id, -1, OUT0); verboseRun(id, -2, OUT1); }
    const uint32_t vh = vramHash(verbose, id);
    for (uint32_t i = 0; i < 2048; i++) SPUBUF[i] = 0;
    spuDma(0x0000, SPUBUF, 2048, false);
    const uint32_t sc = hashw(SPUBUF, 1024), sd = hashw(SPUBUF + 1024, 1024);
    if (verbose) { printf("VP %u", id); for (int k = 0; k < 32; k++) printf(" %x", hashw(SPUBUF + k * 64, 64)); printf(" %x\n", SPU(0x19C) | (SPU(0x19E) << 16)); }
    printf("c %u %u %u %x %x %x %x %x %x %x %x %x\n", id, OUT0[0], OUT1[0], gprHash(OUT0), gprHash(OUT1),
           hashw(OUT0 + 128, 768), hashw(OUT1 + 128, 768), hashw(OUT0 + 35, 64), hashw(OUT1 + 35, 64), vh, sd, sc);
    BREAD[1] = 0;
}

int main() {
    /* Timer 2 from here wraps every 49,152 cycles (32 SPU sample pairs of 1,536, 192 refresh periods of 256): wrap.s aligns
     * each run on (T2 - e) mod 1536, so every case starts at the same place on the refresh grid AND the SPU's sample clock,
     * whatever ran before it. Times above 49,151 cycles alias (none in the generator's range). */
    R16(0x1F801128u) = 49151;
    R16(0x1F801124u) = 0x0008;                 /* system clock, reset after reaching the target */
    const uint16_t e = T2;                     /* the anchor: the runner's locked start put us at a fixed refresh phase */
    printf("\nfuzz/fuzz-all\n");
    EnterCriticalSection();
    const uint32_t mask0 = I_MASK;
    I_MASK = 0;
    fz_setsr(fz_getsr() | 0x40000000u);        /* COP2 on */
    /* the table: appended after the image (tools/fuzzall.js), copied out before anything can use that RAM */
    const uint32_t *src = (const uint32_t *)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 4096 && src[0] != 0x46555A31u; i++) src += 4;   /* it starts where the file's image ended */
    if (src[0] != 0x46555A31u) { printf("no case table at %x\nDone.\n", (uint32_t)src); for (;;) {} }
    const uint32_t tw = src[4] < TABLE_MAX ? src[4] : TABLE_MAX;
    for (uint32_t i = 0; i < tw; i++) TABLE[i] = src[i];
    for (int i = 0; i < 256; i++) SCRSAVE[i] = R32(0x1F800000u + i * 4);
    const uint32_t nCases = TABLE[1], flags = TABLE[2], bseed = TABLE[3];
    codeOff = (((uint32_t)fz_wrap_end - (uint32_t)fz_wrap_start) + 15) & ~15u;
    CTX[1] = e;
    dpcr0 = DPCR; dicr0 = DICR;
    DPCR = dpcr0 | 0x08080888u;                /* channels 0, 1, 2, 4 and 6 on */
    CTX[112] = DPCR; CTX[113] = dicr0 & 0x00FFFFFFu;
    SPU(0x180) = 0; SPU(0x182) = 0;            /* main volume 0: nothing out of the jack */
    SPU(0x184) = 0; SPU(0x186) = 0; SPU(0x1B0) = 0; SPU(0x1B2) = 0; SPU(0x1B4) = 0; SPU(0x1B6) = 0;
    SPU(0x1AC) = 0x0004;
    spucnt = 0xC000; SPU(0x1AA) = spucnt;
    /* the texture region (128,256)-(255,319): from the batch seed, once */
    rng = bseed | 1;
    for (int i = 0; i < 4096; i++) TEXBUF[i] = rnd();
    gpuWait(26); gpuWait(28);
    GP0 = 0x01000000u;
    GP0 = 0xA0000000u; GP0 = (256u << 16) | 128u; GP0 = (64u << 16) | 128u;
    gpuDmaWords(TEXBUF, 4096);
    printf("cases %u codeOff %x flags %x\n", nCases, codeOff, flags);
    uint32_t at = 5;
    for (uint32_t n = 0; n < nCases && at < tw; n++) {
        const uint32_t *c = TABLE + at;
        lite = (flags & 6) != 0;
        regsOnly = (flags & 4) != 0;
        dumpVram = (flags & 8) != 0;
        runCase(c, flags & 1);
        at += 4 + c[2] + 2 * c[3];
    }
    DPCR = dpcr0; DICR = dicr0;
    SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
    for (int i = 0; i < 256; i++) R32(0x1F800000u + i * 4) = SCRSAVE[i];
    I_MASK = mask0;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
