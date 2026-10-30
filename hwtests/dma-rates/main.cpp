/*
 * dma/dma-rates - what the CPU sees of a DMA transfer on the channels nobody
 * timed: OTC and the other SyncMode 0 starts, the MDEC and SPU channels'
 * SyncMode 1 transfers, and a GPU upload beside a CPU reading the BIOS ROM.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, GPU rows 44, 46, 48, 52 and 54; CPU row
 * Z). ps1-tests' dma/chopping swept the GPU channel's SyncMode 1 transfers
 * block size by block size, and M39-M226 fitted that channel to it. Its own
 * source says the rest: "Only the GPU channel: the sweep is DMA2 and nothing
 * measures the others." What the others do here:
 *   - SYNCMODE 0 (OTC, and a CD read without chopping): the transfer runs the
 *     moment CHCR is written, the CPU stopped for all of it - no start
 *     latency, "unmeasured, not known to be free" (row 54). OTC's word is one
 *     cycle (psx-spx's table; row 46).
 *   - SYNCMODE 1 ON THE MDEC AND SPU CHANNELS: M39's one-time 230 cycles
 *     charged as a stall at the start, no gap between blocks, and Busy clear
 *     the moment the last word moves (row 48). The GPU channel has had that
 *     230 split into a latency and a Busy that lingers since M155-M161; the
 *     other channels were never measured to need it.
 *   - THE SPU'S WORD: 68 cycles (M169: a halfword per 32-cycle SPU slot and
 *     the FIFO's refill, mednafen's "closer to 69, average"). spu/memory-
 *     transfer bounds it (6.4-70 a word) without printing it (row 44).
 *   - A GPU BLOCK AND THE BIOS ROM: a CPU load from ROM holds the next GPU
 *     block like a RAM access does (row 52: an assumption; the Sony capture
 *     cannot rule, TUTO3 never reads ROM during a transfer).
 * psx-spx says the CPU "is stopped during DMA" and runs between SyncMode 1
 * blocks and SyncMode 2 list entries - how long each start takes, and how much
 * a CPU gets done inside a transfer, it does not say.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, every delta summed
 * so a long transfer survives the counter's wrap. Each case writes CHCR, does K
 * units of work, reads CHCR once (its Busy bit, and when), then polls it with
 * K units of work a turn until Busy clears: the first read, the total cycles
 * and the turns. A CONTROL writes 0 to the same CHCR (nothing starts): the
 * bracket's own cost. POLL ALONE runs each loop body with no DMA, for the
 * cost of a turn.
 *   OTC      DMA6, N = 1, 2, 16, 256, 1024 words; K = 0, and K = 64 ALU ops
 *            before the first read.
 *   MDEC IN  DMA0 carrying the quantisation tables after the CPU writes the
 *            command (40000001h: 32 words; 40000000h: 16), in one block of 32,
 *            two of 16 and four of 8; K = 0 and 16.
 *   SPU      DMA4 into sound RAM at 8000h, 1, 4, 16 and 64 blocks of 16 words,
 *            16 ALU ops a turn.
 *   GPU      DMA2, a 128x128 CPU-to-VRAM upload in 512 blocks of 16 (as
 *            dma-contention), with the CPU spinning, doing 16 ALU ops a turn,
 *            16 RAM loads a turn, or 16 BIOS ROM loads a turn.
 *
 * Every case runs twice, set up afresh each time, and the second run (with the
 * instruction cache warm) is the one printed: cold, the 64 ALU ops alone cost
 * 44 cycles more here.
 *
 * READ IT LIKE THIS. A first read with Busy SET on OTC or MDEC means the CPU
 * was not stopped from the write; its time is then the start latency. The
 * total minus the CONTROL, per word, is the channel's rate as the CPU sees it.
 * Turns x K is the CPU's work inside the transfer.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js dma-rates`, seconds):
 *   OTC      the first read idle at every N, at the CONTROL's 11 plus N: the
 *            CPU stopped from the write for one cycle a word. The 64 ALU ops
 *            do not overlap it (1,024 words: 1,099 = 75 + 1,024).
 *            THE CONSOLES (console day, all six runs alike): the first read
 *            BUSY at 17 / 18 / 32 / 283-287 / 1,100-1,110 for N = 1 / 2 / 16 /
 *            256 / 1,024, the next idle; with 64 ALU ops idle at 75 for N <= 16,
 *            291 at 256, 1,099-1,114 at 1,024 - the CPU runs through it, a bus
 *            access waits for its end. AT v0.377.35 (src/psx.js DMA_OTC_*):
 *            17 / 18 / 32 / 287 / 1,100 BUSY, 73 / 73 / 73 / 291 / 1,104 idle
 *            (its CONTROL rows 10 / 73, the consoles' 12 / 75).
 *   MDEC IN  idle at the first read, 273 for 32 words in any block split and
 *            257 for 16: the 230-cycle start stall, then a cycle a word, no
 *            gap between blocks.
 *   SPU      one block 273, idle at the first read (the stall and a cycle a
 *            word into an empty FIFO); from the second block on the CPU runs
 *            and each block waits for the SPU: 64 blocks 68,840 cycles, 67.22 a
 *            word, 1,987 turns of 34 cycles (a turn alone is 33.95).
 *            THE CONSOLES (console day, 2026-10-05): one block 150, idle at the
 *            first read; 4 / 16 / 64 blocks 3,616 / 17,500 / 72,796 (7501;
 *            9001 3,592 / 17,470 / 72,766); the 5501's first run 86 and 1,110
 *            a block. AT v0.372 (M372, the SPU's transfer accesses): 150; 3,578
 *            / 17,438 / 72,734 cycles, 85 / 429 / 1,801 turns.
 *   GPU      512 blocks: SPIN 14,088; 16 ALU ops a turn 14,104 (414 turns);
 *            16 RAM loads 14,124 (32 turns, 441 cycles each against 131 with
 *            no DMA); 16 ROM loads 23,626 - each ROM load holds the next
 *            block, and the upload takes 9,500 cycles longer.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define MADR(c) (0x1F801080u + 0x10u * (c))
#define BCR(c)  (0x1F801084u + 0x10u * (c))
#define CHCR(c) (0x1F801088u + 0x10u * (c))
#define GP0     0x1F801810u
#define GP1     0x1F801814u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SPUREG(o) (0x1F801C00u + (o))

struct Res { uint32_t first, firstAt, total, iters; };

/* K units of work: KIND 0 an ALU op, 1 a load from `base`. */
#define WORK ".if %[kind] == 0\n\t.rept %[k]\n\taddu $t5, $t5, $t4\n\t.endr\n\t.else\n\t" \
             ".rept %[k]\n\tlw $t4, 0(%[base])\n\t.endr\n\t.endif\n\t"

/* Write CHCR, K units of work, read CHCR once, then poll it (K units a turn) until Busy (bit 24) clears. Every
 * Timer 2 delta is summed, so the total survives the counter's wrap. */
template <int K, int KIND> __attribute__((noinline)) void startPoll(uint32_t chcr, uint32_t val, uint32_t base, Res* r) {
    uint32_t first, firstAt, total, iters;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t.set noat\n\t"
        "lui $t9, 0x1f80\n\t"
        "lhu $t0, 0x1120($t9)\n\t"
        "nop\n\t"
        "sw %[val], 0(%[chcr])\n\t"
        WORK
        "lw %[first], 0(%[chcr])\n\t"
        "lhu $t6, 0x1120($t9)\n\t"
        "nop\n\t"
        "subu %[firstAt], $t6, $t0\n\t"
        "andi %[firstAt], %[firstAt], 0xffff\n\t"
        "move %[total], %[firstAt]\n\t"
        "move $t0, $t6\n\t"
        "li %[iters], 0\n\t"
        "srl $t3, %[first], 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "beqz $t3, 2f\n\t"
        "nop\n"
        "1:\n\t"
        WORK
        "lw $t3, 0(%[chcr])\n\t"
        "lhu $t6, 0x1120($t9)\n\t"
        "addiu %[iters], %[iters], 1\n\t"
        "subu $t7, $t6, $t0\n\t"
        "andi $t7, $t7, 0xffff\n\t"
        "addu %[total], %[total], $t7\n\t"
        "move $t0, $t6\n\t"
        "srl $t3, $t3, 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "beqz $t3, 2f\n\t"
        "sltiu $t3, %[iters], 0x7fff\n\t"          /* a limit of 32,767 turns (see pollAlone) */
        "bnez $t3, 1b\n\t"
        "nop\n"
        "2:\n\t"
        ".set pop\n\t"
        : [first] "=&r"(first), [firstAt] "=&r"(firstAt), [total] "=&r"(total), [iters] "=&r"(iters)
        : [chcr] "r"(chcr), [val] "r"(val), [base] "r"(base), [k] "i"(K), [kind] "i"(KIND)
        : "t0", "t3", "t4", "t5", "t6", "t7", "t9", "memory");
    r->first = first; r->firstAt = firstAt; r->total = total; r->iters = iters;
    /* THE TURN LIMIT (console day, 2026-10-05): on the SCPH-5501 DMA0 into the MDEC never finished, and this poll -
     * with interrupts off - left the console deaf to the cable until a reset. 32,767 turns is some twenty times the
     * longest transfer here (8,192 GPU words, ~1,500 turns); pollAlone carries the same two instructions, so its
     * turns cost what these do. Given up: the channel is stopped, and rest() says so. */
    if (iters >= 0x7FFF) write32(chcr, read32(chcr) & ~0x01000000u);
}

/* The poll loop's body `n` times with no DMA: the cycles of `n` turns. */
template <int K, int KIND> __attribute__((noinline)) uint32_t pollAlone(uint32_t chcr, uint32_t base, uint32_t n) {
    uint32_t total;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t.set noat\n\t"
        "lui $t9, 0x1f80\n\t"
        "lhu $t0, 0x1120($t9)\n\t"
        "li %[total], 0\n"
        "1:\n\t"
        WORK
        "lw $t3, 0(%[chcr])\n\t"
        "lhu $t6, 0x1120($t9)\n\t"
        "addiu %[n], %[n], -1\n\t"
        "subu $t7, $t6, $t0\n\t"
        "andi $t7, $t7, 0xffff\n\t"
        "addu %[total], %[total], $t7\n\t"
        "move $t0, $t6\n\t"
        "srl $t3, $t3, 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "bnez $zero, 1b\n\t"                       /* never taken: startPoll's limit check, cycle for cycle */
        "sltiu $t3, %[n], 0x7fff\n\t"
        "bnez %[n], 1b\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : [total] "=&r"(total), [n] "+r"(n)
        : [chcr] "r"(chcr), [base] "r"(base), [k] "i"(K), [kind] "i"(KIND)
        : "t0", "t3", "t4", "t5", "t6", "t7", "t9", "memory");
    return total;
}

typedef void (*pollFn)(uint32_t, uint32_t, uint32_t, Res*);

uint32_t otcBuf[1024] __attribute__((aligned(16)));
uint32_t mdecBuf[32] __attribute__((aligned(16)));
uint32_t spuBuf[1024] __attribute__((aligned(16)));
uint32_t gpuBuf[8192] __attribute__((aligned(16)));
uint32_t loadCell[4] __attribute__((aligned(16)));

static void rest(const Res& r) {
    printf("   first read %s at %6u   total %7u   turns %5u%s\n",
           (r.first >> 24) & 1 ? "BUSY" : "idle", (unsigned)r.firstAt, (unsigned)r.total, (unsigned)r.iters,
           r.iters >= 0x7FFF ? "   GAVE UP: still busy (channel stopped)" : "");
}
static void show(const char* name, const Res& r) { printf("  %-24s", name); rest(r); }

static void spuMode(uint16_t& cnt, int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPUREG(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPUREG(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { }
}

static void gpuWaitIdle() {
    for (int i = 0; i < 1000000 && (read32(GP1) & 0x14000000) != 0x14000000; i++) { }
}
static void gpuBeginUpload(uint32_t x, uint32_t y) {
    write32(GP1, 0x01000000);                               /* reset the command buffer */
    gpuWaitIdle();
    write32(GP0, 0xA0000000); write32(GP0, (y << 16) | x); write32(GP0, (128u << 16) | 128u);
    write32(GP1, 0x04000002);                               /* DMA direction: CPU -> GP0 */
}

int main() {
    printf("\ndma/dma-rates\n");
    printf("DMA starts, rates and the CPU's share on the channels chopping did not sweep. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x08080808u);              /* DMA0, 2, 4, 6 enabled */
    for (int i = 0; i < 8192; i++) gpuBuf[i] = 0x7FFF0000u | (uint32_t)i;
    for (int i = 0; i < 1024; i++) spuBuf[i] = 0;
    for (int i = 0; i < 32; i++) mdecBuf[i] = 0x01010101u * (uint32_t)(i + 1);
    const uint32_t rom = 0xBFC00000u, ram = 0xA0000000u | ((uint32_t)loadCell & 0x1FFFFFu);
    Res r;

    printf("Cycles at the system clock. 'first read' is CHCR read once after the write (and K units of work).\n");
    printf("OTC (DMA6, SyncMode 0, CHCR 11000002h):\n");
    for (int w = 0; w < 2; w++) startPoll<0, 0>(CHCR(6), 0, 0, &r);
    show("CONTROL (CHCR = 0)", r);
    for (int w = 0; w < 2; w++) startPoll<64, 0>(CHCR(6), 0, 0, &r);
    show("CONTROL, 64 ALU ops", r);
    static const uint32_t otcN[] = { 1, 2, 16, 256, 1024 };
    for (unsigned i = 0; i < 5; i++) {
        const uint32_t n = otcN[i];
        for (int k = 0; k < 2; k++) {
            for (int w = 0; w < 2; w++) {
                write32(MADR(6), (uint32_t)&otcBuf[n - 1] & 0x1FFFFFu);
                write32(BCR(6), n);
                if (k == 0) startPoll<0, 0>(CHCR(6), 0x11000002u, 0, &r);
                else startPoll<64, 0>(CHCR(6), 0x11000002u, 0, &r);
            }
            printf("  %4u words%s", (unsigned)n, k ? ", 64 ALU ops" : "            ");
            rest(r);
        }
    }
    printf("  (table check: the first entry %08x, the last %08x)\n", otcBuf[0], otcBuf[1023]);

    printf("\nMDEC IN (DMA0, SyncMode 1, CHCR 01000201h), the quantisation tables after the CPU writes the command:\n");
    for (int w = 0; w < 2; w++) startPoll<0, 0>(CHCR(0), 0, 0, &r);
    show("CONTROL (CHCR = 0)", r);
    struct Mc { const char* name; uint32_t cmd, bs, blocks; };
    static const Mc mc[] = {
        { "32 words, 1 x 32", 0x40000001u, 32, 1 },
        { "32 words, 2 x 16", 0x40000001u, 16, 2 },
        { "32 words, 4 x 8", 0x40000001u, 8, 4 },
        { "16 words, 1 x 16", 0x40000000u, 16, 1 },
    };
    for (unsigned i = 0; i < 4; i++)
        for (int k = 0; k < 2; k++) {
            for (int w = 0; w < 2; w++) {
                write32(MDEC1, 0x80000000u);                /* reset */
                write32(MDEC1, 0x60000000u);                /* data-in and data-out requests on */
                write32(MDEC0, mc[i].cmd);
                write32(MADR(0), (uint32_t)mdecBuf & 0x1FFFFFu);
                write32(BCR(0), (mc[i].blocks << 16) | mc[i].bs);
                if (k == 0) startPoll<0, 0>(CHCR(0), 0x01000201u, 0, &r);
                else startPoll<16, 0>(CHCR(0), 0x01000201u, 0, &r);
            }
            printf("  %s%s", mc[i].name, k ? ", 16 ALU" : "        ");
            rest(r);
        }
    write32(MDEC1, 0x80000000u);

    printf("\nSPU (DMA4 write to sound RAM 8000h, SyncMode 1, blocks of 16 words, 16 ALU ops a turn):\n");
    uint16_t cnt = (uint16_t)(read16(SPUREG(0x1AA)) | 0x8000);
    write16(SPUREG(0x1AC), 0x0004);
    pollAlone<16, 0>(CHCR(4), 0, 200);
    const uint32_t spuAlone = pollAlone<16, 0>(CHCR(4), 0, 200);
    printf("  a turn with no DMA: %u.%02u cycles\n", (unsigned)(spuAlone / 200), (unsigned)(spuAlone % 200 / 2));
    static const uint32_t spuBlocks[] = { 1, 4, 16, 64 };
    for (unsigned i = 0; i < 4; i++) {
        for (int w = 0; w < 2; w++) {
            spuMode(cnt, 0);
            write16(SPUREG(0x1A6), 0x8000 / 8);
            spuMode(cnt, 2);
            write32(MADR(4), (uint32_t)spuBuf & 0x1FFFFFu);
            write32(BCR(4), (spuBlocks[i] << 16) | 16);
            startPoll<16, 0>(CHCR(4), 0x01000201u, 0, &r);
        }
        printf("  %3u blocks (%4u words)", (unsigned)spuBlocks[i], (unsigned)spuBlocks[i] * 16);
        rest(r);
        printf("    per word %u.%02u\n", (unsigned)(r.total / (spuBlocks[i] * 16)), (unsigned)(r.total * 100 / (spuBlocks[i] * 16) % 100));
    }
    spuMode(cnt, 0);

    printf("\nGPU (DMA2, a 128x128 upload, 512 blocks of 16 words):\n");
    struct Gc { const char* name; pollFn f; uint32_t base; };
    static const Gc gc[] = {
        { "SPIN", startPoll<0, 0>, 0 },
        { "16 ALU ops a turn", startPoll<16, 0>, 0 },
        { "16 RAM loads a turn", startPoll<16, 1>, 0 },
        { "16 ROM loads a turn", startPoll<16, 1>, 0 },
    };
    for (unsigned i = 0; i < 4; i++) {
        const uint32_t base = i == 2 ? ram : rom;
        for (int w = 0; w < 2; w++) {
            gpuBeginUpload(0, 256);
            write32(MADR(2), (uint32_t)gpuBuf & 0x1FFFFFu);
            write32(BCR(2), (512u << 16) | 16);
            gc[i].f(CHCR(2), 0x01000201u, base, &r);
            gpuWaitIdle();
        }
        show(gc[i].name, r);
    }
    write32(GP1, 0x04000000);
    pollAlone<16, 0>(CHCR(2), 0, 200);
    const uint32_t aAlu = pollAlone<16, 0>(CHCR(2), 0, 200);
    pollAlone<16, 1>(CHCR(2), ram, 200);
    const uint32_t aRam = pollAlone<16, 1>(CHCR(2), ram, 200);
    pollAlone<16, 1>(CHCR(2), rom, 200);
    const uint32_t aRom = pollAlone<16, 1>(CHCR(2), rom, 200);
    printf("  a turn with no DMA: ALU %u.%02u, RAM %u.%02u, ROM %u.%02u cycles\n",
           (unsigned)(aAlu / 200), (unsigned)(aAlu % 200 / 2), (unsigned)(aRam / 200), (unsigned)(aRam % 200 / 2),
           (unsigned)(aRom / 200), (unsigned)(aRom % 200 / 2));

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-rates: Done.\n");
    for (;;) VSync(0);
}
