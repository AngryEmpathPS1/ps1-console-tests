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
#include <stdarg.h>

extern "C" {
void fz_pair(void);
extern uint32_t fz_wrap_start[], fz_wrap_end[];
extern char _end[];
void fz_flush(void);
uint32_t fz_getsr(void);
void fz_setsr(uint32_t);
uint32_t fz_probe(volatile uint16_t *t2, uint32_t target, volatile uint32_t *ramWord);
uint32_t fz_iprobe(volatile uint16_t *t2, uint32_t target, uint32_t stub);
}
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".global fz_flush\nfz_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n"
    ".global fz_getsr\nfz_getsr:\n  mfc0 $v0, $12\n  jr $ra\n  nop\n"
    ".global fz_setsr\nfz_setsr:\n  mtc0 $a0, $12\n  nop\n  jr $ra\n  nop\n"
    /* fz_align / fz_probe: hwtests/runner's runner_align and runner_probe as they are (a spin to within 240 counts, one more read,
     * a slide down 256 cached nops; then one RAM load between two reads of Timer 2, longer by the stall when a DRAM refresh is on
     * the bus). Registers only, no stack: ra kept in t9.
     * THE SPIN IS BOUNDED (2026-11-18): 131,072 polls (~0.9 M cycles, 18 laps of this host's Timer 2), then it gives up and returns -
     * rfmap2 (these probes before the first case and after the last) left the SCPH-5501-u2 silent for 180 s on 2026-11-18 with
     * nobody to reset it; the emulator ran it to its end from both starts (hwrunner, tools/cartrun.js), so what stopped it is not
     * known, and a timer that is not counting is the one way this code could wait for ever. */
    ".global fz_align\nfz_align:\n"
    "  lui $t7, 2\n"
    "1:\n  lhu $t0, 0($a0)\n  addiu $t7, $t7, -1\n  subu $t1, $a1, $t0\n  andi $t1, $t1, 0xFFFF\n  beqz $t7, 3f\n  sltiu $t2, $t1, 240\n  beqz $t2, 1b\n  nop\n"
    "  lhu $t0, 0($a0)\n  nop\n  subu $t1, $a1, $t0\n  andi $t1, $t1, 0xFFFF\n  addiu $t1, $t1, -16\n"
    "  bltz $t1, 3f\n  nop\n"
    /* THE SLIDE STAYS INSIDE ITSELF (2026-11-18): a second read past this host's Timer 2 reset (it resets AT its target 49151, so it
     * never holds 49151) made the distance 49,135 and the jump landed 196 KB below the slide - an instruction bus error at 7FFD24C0h,
     * which the stock kernel turns into DeliverEvent(F0000010h, 1000h) and SystemError, for ever: the cart consoles' silence
     * (experiments/fuzz-all/2026-11-18-steps/stepemu4.js on the cart start: CAUSE 18h, EPC 7FFD24C0h, sp in the kernel's 85C0h). A
     * distance past the slide returns at once. */
    "  sltiu $t2, $t1, 256\n  beqz $t2, 3f\n  nop\n"
    "  la $t3, 2f\n  sll $t1, $t1, 2\n  subu $t3, $t3, $t1\n  jr $t3\n  nop\n"
    "  .rept 256\n  nop\n  .endr\n"
    "2:\n3:\n  jr $ra\n  nop\n"
    ".global fz_probe\nfz_probe:\n"
    "  move $t9, $ra\n  jal fz_align\n  nop\n"
    "  lhu $t4, 0($a0)\n  lw $t5, 0($a2)\n  lhu $t6, 0($a0)\n  nop\n"
    "  subu $v0, $t6, $t4\n  jr $t9\n  andi $v0, $v0, 0xFFFF\n"
    /* fz_iprobe(T2, target, stub): the same with a call to `stub` (jr ra; nop in uncached RAM, KSEG1) between the two reads: two
     * instruction fetches from RAM, as a cold case's code takes them, where fz_probe takes a data load */
    ".global fz_iprobe\nfz_iprobe:\n"
    "  move $t9, $ra\n  jal fz_align\n  nop\n"
    "  lhu $t4, 0($a0)\n  jalr $a2\n  nop\n  lhu $t6, 0($a0)\n  nop\n"
    "  subu $v0, $t6, $t4\n  jr $t9\n  andi $v0, $v0, 0xFFFF\n"
    ".set pop\n");

/* THE REFRESH PHASE OF THIS RUN'S TIMER 2 (2026-11-17, the console fuzzer; main's "align the host itself"): the two cart consoles
 * start every case 19 cycles from where the four Unirom-booted ones do on the DRAM refresh's 256-cycle grid - batch 10801's table on
 * the phase host at P = 19 gives the SCPH-5501-u2's own batch back 57 / 60 cold and warm on the SCPH-7001, P 18 / 20 42-48, P 0 26
 * (experiments/fuzz-all/2026-11-17-cartsplit/phasefit.js) - though the runner locks the refresh: the cart's runner lands elsewhere.
 * So the host finds the refresh itself, the runner's way (probeRefresh): the probe at each of the 256 phases of Timer 2, twice (the
 * first pass fills the I-cache), the longest read the stall, refR0 its first phase. Timer 2 wraps at 49152 here, a multiple of
 * 256, so the phase holds for the whole run; a pair of reads across the wrap reads 16384 long and is taken back. */
static volatile uint32_t probeWord;
static uint32_t iStub[4] __attribute__((aligned(16))) = { 0x03E00008u, 0, 0, 0 };   /* jr ra; nop - called through KSEG1 */
static uint32_t refR0;
static uint8_t tab[256], itab[256];
/* the 256-phase tables both ways: data load (tab) and two uncached instruction fetches (itab); the cart consoles' cases run as if
 * started 19 cycles off the Unirom ones' (P 19 + 256k: 57 / 60) where the data-load probe reads the same refR0 on all six (70) */
static void probeRefresh() {
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t p = 0; p < 256; p++) {
            /* the target never in the block before the reset (48896-49151): 49151 itself is never read, so the spin's last look and the
             * slide's read can fall either side of the wrap - that block's target goes in the next lap's first block */
            uint32_t base = (((uint32_t)*(volatile uint16_t *)0x1F801120u + 600u) % 49152u) & ~0xFFu; if (base >= 48896u) base = 0;
            const uint32_t tgt = base | p;
            uint32_t d = fz_probe((volatile uint16_t *)0x1F801120u, tgt, &probeWord);
            if (d >= 16384u) d -= 16384u;   /* across the wrap: (t6 - t4) & FFFFh = elapsed + 65536 - 49152 (>= 40000 missed them: RFR 70 on all six) */
            if (pass) tab[p] = d > 255 ? 255 : (uint8_t)d;
            uint32_t base2 = (((uint32_t)*(volatile uint16_t *)0x1F801120u + 600u) % 49152u) & ~0xFFu; if (base2 >= 48896u) base2 = 0;
            const uint32_t tg2 = base2 | p;
            uint32_t di = fz_iprobe((volatile uint16_t *)0x1F801120u, tg2, ((uint32_t)iStub & 0x1FFFFFFFu) | 0xA0000000u);
            if (di >= 16384u) di -= 16384u;
            if (pass) itab[p] = di > 255 ? 255 : (uint8_t)di;
        }
    uint32_t best = 0;
    for (uint32_t p = 1; p < 256; p++) if (tab[p] > tab[best]) best = p;
    for (int i = 0; i < 255 && tab[(best + 255) & 255] == tab[best]; i++) best = (best + 255) & 255;   /* the run's first phase */
    refR0 = best;
}

#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))

/* THE DB-25 OUT (2026-11-17; AngryEmpath's exception to the 1F000000h-1F07FFFFh ban, through main: "Allow it"). SCOPE, exactly:
 *   - this HOST code only, never a generated case (tools/fuzzall.js's generator ban is unchanged);
 *   - writes to 1F060008h ONLY (the GameShark's comms-out byte); reads of 1F060000h (the PC's byte) and 1F020010h (STB) for the handshake;
 *   - a console whose cart is a GameShark only: the host REFUSES unless the cart ROM's cold block at 80190000h ("AESNAP01", its
 *     STAGE record's CART_KIND) says 1 (the GameShark build; 2 is the Power Replay, no comms link) and the Pico's STB is up.
 * Every other address in 1F000000h-1F07FFFFh stays banned for this host and its cases alike. The handshake is cartrom/rom.c's
 * comms_put: wait for STB (bounded: 2,000 reads, then the stream is off for the run), take the PC's byte, write ours (the write pulses ACK).
 * Asked for by the table's flags: bit 4 a copy of every printed line out the DB-25 as well; bit 5 the verbose lines (V*, DRW) out the
 * DB-25 ONLY, the SIO1 log keeping the result lines (the runner's 262,144-character log no longer caps a verbose batch). The
 * receiving end: tools/dbcapread.js on the Pico's bridge port (127.0.0.1:3440). Without the flags, or refused, the host prints as before. */
#define COMMS_STB (*(volatile uint8_t *)0x1F020010u)
#define COMMS_IN (*(volatile uint8_t *)0x1F060000u)
#define COMMS_OUT (*(volatile uint8_t *)0x1F060008u)
static int dbOn, dbVonly, dbLines;
static uint32_t dbBytes;
static int cartIsGameShark(void) {   /* the cold block's frame: records tag(1) width(1) count(2) address(4) values, ended by tag FFh */
    const uint8_t *p = (const uint8_t *)0x80190000u;
    static const char M[8] = { 'A', 'E', 'S', 'N', 'A', 'P', '0', '1' };
    for (int i = 0; i < 8; i++) if (p[i] != (uint8_t)M[i]) return 0;
    uint32_t o = 8;
    while (o + 8 <= 256) {
        const uint32_t tag = p[o], w = p[o + 1], cnt = p[o + 2] | (p[o + 3] << 8);
        if (tag == 0xFF || !w || w > 4) return 0;
        if (tag == 0x0F && cnt >= 3) { uint32_t v = 0; for (uint32_t b = 0; b < w; b++) v |= (uint32_t)p[o + 8 + 2 * w + b] << (8 * b); return v == 1; }
        o += 8 + w * cnt;
    }
    return 0;
}
static void dbPut(uint32_t b) {
    if (!dbOn) return;
    for (uint32_t n = 0; !(COMMS_STB & 1); n++) if (n > 2000) { dbOn = 0; return; }
    (void)COMMS_IN;
    COMMS_OUT = (uint8_t)b;
    dbBytes++;
}
/* every printed line goes through here: the BIOS's printf (SIO1, the runner) and, when on, the DB-25 */
/* the line buffer at a fixed place, 8010C000h-8010FFFFh (TEXBUF's end to TABLE): in BSS its 8 KB pushed _end past the file's end, so
 * the case table appended after the image lay inside BSS and the start-up's clearing wiped it ("no case table") */
#define hline ((char *)0x8010C000u)
#define HLINE_MAX 8192u
static uint32_t hlen;
static void hflush(void) {
    hline[hlen] = 0;
    const int verbose = hline[0] == 'V' || (hline[0] == 'D' && hline[1] == 'R' && hline[2] == 'W');
    if (!(dbOn && dbVonly && verbose)) printf("%s", hline);
    if (dbLines) for (uint32_t i = 0; i < hlen; i++) dbPut((uint8_t)hline[i]);
    hlen = 0;
}
static void hput(char c) { if (hlen >= HLINE_MAX - 1) hflush(); hline[hlen++] = c; if (c == 10) hflush(); }
static void hnum(uint32_t v, uint32_t base, int neg) {
    char t[12]; int n = 0;
    if (neg) hput('-');
    do { const uint32_t d = v % base; t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    while (n) hput(t[--n]);
}
static void hout(const char *f, ...) {   /* the host's formats only: %u %d %x %c %s */
    va_list ap; va_start(ap, f);
    for (; *f; f++) {
        if (*f != '%') { hput(*f); continue; }
        const char k = *++f;
        if (k == 'u') hnum(va_arg(ap, uint32_t), 10, 0);
        else if (k == 'x') hnum(va_arg(ap, uint32_t), 16, 0);
        else if (k == 'd') { const int v = va_arg(ap, int); hnum(v < 0 ? (uint32_t)-v : (uint32_t)v, 10, v < 0); }
        else if (k == 'c') hput((char)va_arg(ap, int));
        else if (k == 's') for (const char *s = va_arg(ap, const char *); *s; s++) hput(*s);
        else if (k == '%') hput('%');
        else if (!k) break;
    }
    va_end(ap);
}
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
/* TIMER 2 AS THE PROBE NEEDS IT, CHECKED FIRST (2026-11-18): the probes after the host's setup (RFR2, RFR3) left BOTH cart consoles
 * silent (the 5501-u2 on rfmap2, the 1001 on rfmap3; the 7001 ran it in 10 s; the probe at the anchor ran on all six), and a Timer 2
 * not counting, or reset at another target, is the one way they could run that long (each align bounded at 131,072 polls). So the
 * T2S line says what Timer 2 is before each later probe - its mode (bits 0-9: reading clears only 11-12), its target and two counts
 * a few cycles apart - and the probe runs only if the target is still 49150 (main's, below) and the count moved. */
static bool t2Check(const char *tag) {
    const uint32_t md = *(volatile uint16_t *)0x1F801124u & 0x3FFu, tg = *(volatile uint16_t *)0x1F801128u;
    const uint32_t c1 = *(volatile uint16_t *)0x1F801120u;
    for (volatile int i = 0; i < 20; i++) {}
    const uint32_t c2 = *(volatile uint16_t *)0x1F801120u;
    const bool ok = tg == 49150u && c1 != c2 && c1 < 49152u && c2 < 49152u;
    hout("T2S %s %x %u %u %u%s\n", tag, md, tg, c1, c2, ok ? "" : " - the probe skipped");
    return ok;
}
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
        if (dumpVram) hout("VW %u %u", id, y);   /* flags bit 3: every word of the draw area (a VRAM dump, 64 lines) */
        for (uint32_t x = 0; x < 64; x++) { const uint32_t w = GPUREAD; h = fnv(h, w); r = fnv(r, w); if (dumpVram) hout(" %x", w); }
        if (dumpVram) hout("%c", 10);
        rows[y] = r;
    }
    GP0 = 0xC0000000u; GP0 = (256u << 16) | 128u; GP0 = (1u << 16) | 2u;   /* GPUREAD left on a word of the fixed texture region */
    gpuWait(27);
    (void)GPUREAD;
    if (verbose) { hout("VR %u", id); for (int y = 0; y < 64; y++) hout(" %x", rows[y]); hout("\n"); }
    return h;
}

static bool regsOnly;
static void verboseRun(uint32_t id, int run, const uint32_t *o) {
    const bool vgOnly = run < 0;
    if (vgOnly) run = -run - 1;
    static const uint8_t regs[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 20, 21, 22, 23, 24, 25 };
    hout("VG %u %d %u", id, run, o[0]);
    for (uint32_t i = 0; i < sizeof regs; i++) hout(" %x", o[regs[i]]);
    hout(" %x %x %x\n", o[33], o[34], o[99]);
    if (vgOnly && regsOnly) return;   /* flags bit 2: the registers alone, for probes of 350 cases */
    hout("VX %u %d", id, run);
    for (int i = 0; i < 64; i++) hout(" %x", o[35 + i]);
    if (vgOnly) { hout("\n"); return; }   /* lite: registers and the GTE's */
    hout("\nVD %u %d", id, run);
    for (int i = 0; i < 512; i++) if (o[128 + i] != DATAINIT[i]) hout(" %x:%x", i * 4, o[128 + i]);
    hout("\nVS %u %d", id, run);
    for (int i = 0; i < 256; i++) if (o[640 + i] != SCRINIT[i]) hout(" %x:%x", i * 4, o[640 + i]);
    hout("\n");
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
/* THE STEP BREADCRUMB (2026-11-18, main for AngryEmpath): BREAD[2] = the step each case has reached, in RAM on every console, and with
 * flags bit 27 one byte per step OUT THE DB-25 as well (the scope above: a GameShark and the Pico's STB; the Pico timestamps each), so
 * a hang names its last step live, with no RESET and no trust in RAM - the SCPH-1001's record of 2026-11-18 had case 23's last RAM
 * breadcrumb and not case 24's first, with only the loop between, and the RAM at 80100000h had not kept its contents through the reset
 * (experiments/fuzz-all/2026-11-13/SAFE-MODE.md). The bytes: C0h + (id & 3Fh) a case begins; 82h its setup done; 83h the flush done;
 * 84h its two runs done; 85h the verbose readback; 86h the VRAM hash; 87h the sound RAM read; 88h its line printed. In main: 93h
 * the first case next, 9Fh every case done. */
static int dbSteps;
static void step(uint32_t s) { BREAD[2] = s; if (dbSteps) dbPut(s); }
/* flags bit 1 (lite): only the VG lines (time and registers), for probes of a few hundred cases inside the runner's 256 KB log */
static bool lite;
static void runCase(const uint32_t *c, bool verbose) {
    const uint32_t id = c[0], seed = c[1], nCode = c[2], nOver = c[3];
    BREAD[0] = id; BREAD[1] = 1;
    step(0xC0u | (id & 0x3Fu));
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
    /* The rest of the last line, and one line more, zeroed (2026-11-17). Before, the words past a short case were the previous
     * case's. The line fill and the bus latch read them, so a trail rung or a repro read differently from the same case after
     * another one (busvalue: repro-9001-14 alone reads 0, the trail's rung did not). CODEBUF + codeOff is 16-byte aligned. */
    for (uint32_t i = nCode + 2, e = ((nCode + 2 + 3) & ~3u) + 4; i < e; i++) dst[i] = 0;
    CTX[0] = CODEBUF + codeOff;
    setupCase(seed);
    step(0x82);
    fz_flush();
    step(0x83);
    fz_setsr((fz_getsr() & ~1u) | 0x40000000u);
    I_MASK = 0;
    fz_pair();
    BREAD[1] = 2;
    I_MASK = 0; I_STAT = 0;
    step(0x84);
    /* THE LAST DRAW FINISHES BEFORE GP1(01h) (2026-11-19, GPU timing 2's over3: once each run cleared its area, a polygon still drawing
     * here showed cut - at k = 0, no overrun at all, the 24h transposed-texture triangle stopped at row 18 and the textured quads lost
     * their second triangle). GP1(01h) cuts the primitive in progress, so the VRAM read back could measure how far the GPU had got, not
     * what the case drew. Bounded wait for GPUSTAT bit 26 (ready for a command word: 0 while a primitive draws), 100,000 polls as over3b. */
    for (int i = 0; i < 100000 && !(GPUSTAT & 0x04000000u); i++) {}
    GP1 = 0x01000000u; GP1 = 0x04000000u;
    if (verbose) { verboseRun(id, 0, OUT0); verboseRun(id, 1, OUT1); }
    else if (lite) { verboseRun(id, -1, OUT0); verboseRun(id, -2, OUT1); }
    step(0x85);
    const uint32_t vh = vramHash(verbose, id);
    step(0x86);
    for (uint32_t i = 0; i < 2048; i++) SPUBUF[i] = 0;
    spuDma(0x0000, SPUBUF, 2048, false);
    step(0x87);
    const uint32_t sc = hashw(SPUBUF, 1024), sd = hashw(SPUBUF + 1024, 1024);
    if (verbose) { hout("VP %u", id); for (int k = 0; k < 32; k++) hout(" %x", hashw(SPUBUF + k * 64, 64)); hout(" %x\n", SPU(0x19C) | (SPU(0x19E) << 16)); }
    hout("c %u %u %u %x %x %x %x %x %x %x %x %x\n", id, OUT0[0], OUT1[0], gprHash(OUT0), gprHash(OUT1),
           hashw(OUT0 + 128, 768), hashw(OUT1 + 128, 768), hashw(OUT0 + 35, 64), hashw(OUT1 + 35, 64), vh, sd, sc);
    step(0x88);
    BREAD[1] = 0;
}

int main() {
    /* Timer 2 from here wraps every 49,152 cycles (32 SPU sample pairs of 1,536, 192 refresh periods of 256): wrap.s aligns
     * each run on (T2 - e) mod 1536, so every case starts at the same place on the refresh grid AND the SPU's sample clock,
     * whatever ran before it. Times above 49,151 cycles alias (none in the generator's range). */
    /* the three timers' modes and targets as found, put back at the end: the cases change Timer 0 and 1, this host Timer 2,
     * and the programs that run after a fuzz job on the same console should find them as they were (main, 2026-11-17) */
    /* THE BOOT PATH'S SETUP, AS FOUND (2026-11-17): the two cart consoles (the 1001, the 5501-u2: the runner started from the cart, no
     * Unirom) time apart from the other four as a group - seed 10600-10601's noise, 44 cold times split {1001, 5501-u2} against
     * {101, 7001, 7502, 9001} (experiments/fuzz-all/oddone.js) - so every batch records what its boot left: the delay / size registers
     * 1F801008h-1F801020h, RAM size 1F801060h, cache control FFFE0130h, DPCR, DICR, I_MASK and SR. READ ONLY, before anything here
     * writes them (the hard rules: none of these is ever written by this host but DPCR / DICR, put back as found). */
    uint32_t setup[12];
    for (int i = 0; i < 7; i++) setup[i] = R32(0x1F801008u + i * 4);
    setup[7] = R32(0x1F801060u); setup[8] = R32(0xFFFE0130u); setup[9] = R32(0x1F8010F0u); setup[10] = R32(0x1F8010F4u); setup[11] = R32(0x1F801074u);
    const uint32_t sr0 = fz_getsr();
    uint16_t tmode[3], ttgt[3];
    for (int i = 0; i < 3; i++) { tmode[i] = R16(0x1F801104u + i * 16) & 0x03FFu; ttgt[i] = R16(0x1F801108u + i * 16); }
    /* TARGET 49150, A PERIOD OF 49,152 CYCLES (2026-11-19): a target T gives a period of T + 2 cycles on hardware (M104), so the 49151
     * this host set until then ran 49,153 - one cycle past 192 x 256, and the refresh / SPU phase at each case crept one cycle a wrap.
     * The CPU session's rfanchor (experiments/fuzz-all/2026-11-19-rfanchor, consoles 305a0367f): with 49150 the SCPH-7001 and the cart
     * SCPH-1001 read the refresh phase 36 at all three probes, three runs alike; with 49151 they parted by 18 before the first case
     * (the cart's longer setup: more wraps) - the cart split. Every "49152" in this host and wrap.s is now the true period. */
    R16(0x1F801128u) = 49150;
    R16(0x1F801124u) = 0x0008;                 /* system clock, reset after reaching the target */
    const uint16_t e = T2;                     /* the anchor: the runner's locked start put us at a fixed refresh phase */
    hout("\nfuzz/fuzz-all\n");
    EnterCriticalSection();
    const uint32_t mask0 = I_MASK;
    I_MASK = 0;
    fz_setsr(fz_getsr() | 0x40000000u);        /* COP2 on */
    /* the table: appended after the image (tools/fuzzall.js), copied out before anything can use that RAM */
    const uint32_t *src = (const uint32_t *)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 4096 && src[0] != 0x46555A31u; i++) src += 4;   /* it starts where the file's image ended */
    if (src[0] != 0x46555A31u) { hout("no case table at %x\nDone.\n", (uint32_t)src); for (;;) {} }
    const uint32_t tw = src[4] < TABLE_MAX ? src[4] : TABLE_MAX;
    for (uint32_t i = 0; i < tw; i++) TABLE[i] = src[i];
    for (int i = 0; i < 256; i++) SCRSAVE[i] = R32(0x1F800000u + i * 4);
    const uint32_t nCases = TABLE[1], flags = TABLE[2], bseed = TABLE[3];
    codeOff = (((uint32_t)fz_wrap_end - (uint32_t)fz_wrap_start) + 15) & ~15u;
    /* THE START PHASE AS A KNOB (2026-11-15): flags bits 16-26 add 0..1535 cycles to the anchor, so the same case starts that much
     * later on the refresh grid and the SPU's sample pair - one exe per console, each at its own phase, gives a case's phase
     * spread in one pass (tools/fuzzall.js buildExe({ phase })). 0 is the anchor as before: a table without it reads as ever. */
    const uint32_t phase = ((flags >> 16) & 0x7FF) % 1536;
    /* flags bit 7: the refresh probes (the RFR / RFT / RFI lines here, RFR2 before the first case, RFR3 after the last); OFF unless the
     * table asks - one build of them (rfmap2) left the SCPH-5501-u2 silent, cause not found (experiments/fuzz-all/2026-11-13/SAFE-MODE.md),
     * so a batch runs as it did before them. flags bit 6 (needs bit 7): the anchor put on the refresh grid - e moved back until
     * (e - refR0) mod 256 is flags bits 8-15, so every case starts at the same refresh phase whatever runner started this one. The RFR
     * line: refR0, (e - refR0) mod 256 as found, and the anchor used. MEASURED on all six consoles (rfmap, 2026-11-18): the same tables,
     * the data-load stall from phase 80 and the fetch stall 76-87 with e at 7, the cart consoles included - so the carts' 19 cycles are
     * not this grid at the anchor (experiments/fuzz-all/2026-11-17-cartsplit/). */
    const bool refOn = (flags & 0x80) != 0;
    uint32_t eA = e;
    if (refOn) {
        probeRefresh();
        const uint32_t refD = ((uint32_t)e + 256u * 192u - refR0) & 0xFFu;
        if (flags & 0x40) eA = ((uint32_t)e + 49152u - ((refD - ((flags >> 8) & 0xFFu)) & 0xFFu)) % 49152u;
        hout("RFR %u %u %u\n", refR0, refD, eA);
        hout("RFT"); for (int i = 0; i < 256; i++) hout(" %u", tab[i]); hout("\n");
        hout("RFI"); for (int i = 0; i < 256; i++) hout(" %u", itab[i]); hout("\n");
    }
    CTX[1] = (uint16_t)((eA + phase) % 49152);
    dpcr0 = DPCR; dicr0 = DICR;
    /* THE CHANNELS THE CASES READ START AT 0 (2026-11-17): the console runner loads one program behind another with no reset, so
     * DMA0, 1, 4 and 6's MADR / BCR / CHCR held the LAST program's values until a case wrote them - fuzz 10301-8 read DMA6 MADR
     * 1026F4h on the consoles, 10301-29 DMA0 BCR 13h, where the emulator boots each program from reset and read 0. DMA2 the host
     * sets itself below (the texture), before any case. None of the four is running here: nothing is started. */
    static const uint32_t zch[4] = { 0, 1, 4, 6 };
    for (int i = 0; i < 4; i++) { const uint32_t b = 0x1F801080u + zch[i] * 16; R32(b) = 0; R32(b + 4) = 0; R32(b + 8) = 0; }
    /* DPCR AND DICR AS THE UNIROM-BOOTED CONSOLES HAVE THEM, WHATEVER BOOTED THIS ONE (2026-11-17): the SETUP lines of seed 10700 read
     * DPCR 08009899h / DICR 00840000h on the 101, 7001, 7502, 9001 and this emulator, but 076D4321h (the power-on value) / 0 on the two
     * cart consoles (the cart starts the runner with no Unirom and no kernel DMA setup) - so the host's channel enables went in over
     * different channel priorities there, and those two consoles' cold times parted from the other four as a group (seed 10600-10601:
     * 44 noisy cold times split {1001, 5501-u2} / the rest, experiments/fuzz-all/oddone.js). Both are put back as found at the end. */
    DPCR = 0x08009899u | 0x08080888u;          /* channels 0, 1, 2, 4 and 6 on, at the Unirom consoles' priorities */
    CTX[112] = DPCR; CTX[113] = 0x00840000u;
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
    hout("cases %u codeOff %x flags %x\n", nCases, codeOff, flags);
    hout("SETUP");   /* 1008 100C 1010 1014 1018 101C 1020, 1060, FFFE0130, DPCR, DICR, I_MASK, SR - as the boot path left them */
    for (int i = 0; i < 12; i++) hout(" %x", setup[i]);
    hout(" %x\n", sr0);
    /* the DB-25 out, when the table asks for it (flags bits 4-5; the scope above): refused unless the cart is a GameShark and the Pico's
     * STB is up (200 reads, as rom.c's comms_present). The line says which, so every log records whether its DB-25 copy exists. */
    if (flags & 0x08000030u) {
        const int gs = cartIsGameShark();
        int stb = 0; if (gs) for (uint32_t n = 0; n < 200 && !stb; n++) stb = COMMS_STB & 1;
        dbOn = gs && stb; dbVonly = (flags & 0x20) != 0; dbLines = (flags & 0x30) != 0; dbSteps = (flags & 0x08000000u) != 0;
        hout("DB25 %s%s\n", dbOn ? (dbLines ? (dbVonly ? "on verbose-only" : "on copy") : "on") : gs ? "off: no STB (no Pico)" : "refused: not a GameShark", dbOn && dbSteps ? ", step bytes" : "");
    }
    /* THIS BOOT'S DRAWS (2026-11-17, main for AngryEmpath): on a cart console (the SCPH-1001's GameShark, the SCPH-5501-u2's Power
     * Replay, cart ROM v4) the boot left its per-boot draws at 80190100h - the refresh phase, the SPU's sample clock and key-on class,
     * phi (cartrom/README.md "The draws"; tools/cartdraws.js --raw decodes them). The 1,024 bytes as 256 words on one line, so every
     * batch carries the draws of the boot it ran in (tools/fuzzloop.js keeps them per cart console); elsewhere the words are whatever
     * that RAM holds and nothing reads them. */
    hout("DRW");
    for (int i = 0; i < 256; i++) hout(" %x", R32(0x80190100u + i * 4));
    hout("\n");
    step(0x93);
    uint32_t at = 5;
    /* the refresh again, after the setup and just before the first case, and once more after the last (the cart consoles' cases run
     * as if started 19 cycles off where the probe at the anchor reads the same grid on all six: did something in between move it?) */
    if (refOn && t2Check("RFR2")) { probeRefresh(); hout("RFR2 %u %u\n", refR0, ((uint32_t)e + 49152u - refR0) & 0xFFu); }
    for (uint32_t n = 0; n < nCases && at < tw; n++) {
        const uint32_t *c = TABLE + at;
        lite = (flags & 6) != 0;
        regsOnly = (flags & 4) != 0;
        dumpVram = (flags & 8) != 0;
        runCase(c, flags & 1);
        at += 4 + c[2] + 2 * c[3];
    }
    if (refOn && t2Check("RFR3")) { probeRefresh(); hout("RFR3 %u %u\n", refR0, ((uint32_t)e + 49152u - refR0) & 0xFFu); }
    DPCR = dpcr0; DICR = dicr0;
    /* WHAT GP1(00h) DOES NOT RESET, PUT BACK (2026-11-17, the GPU session through main: a fuzz job left the GPU's Timing Control
     * set on the PSone and the 7001, and every GPU timing test after read about a cycle dear; gpu-tri-scan is clean once it is 0).
     * wrap.s writes GP0(03h) 0 before every case too; here for whatever the last case left. Then the timers as found. */
    gpuWait(26);
    GP0 = 0x03000000u;
    for (int i = 0; i < 3; i++) { R16(0x1F801108u + i * 16) = ttgt[i]; R16(0x1F801104u + i * 16) = tmode[i]; }
    SPU(0x18C) = 0xFFFF; SPU(0x18E) = 0x00FF;
    for (int i = 0; i < 256; i++) R32(0x1F800000u + i * 4) = SCRSAVE[i];
    I_MASK = mask0;
    ExitCriticalSection();
    step(0x9F);
    if (flags & 0x08000030u) hout("DB25 bytes %u%s\n", dbBytes, dbOn ? "" : " (the stream went off)");
    hout("\nDone.\n");
    for (;;) {}
}
