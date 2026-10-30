/*
 * dma/dma-contention — during a GPU DMA, does it matter WHAT the CPU does with
 * the cycles it gets back?
 *
 * WHY THIS TEST EXISTS. This core models SyncMode 1 GPU transfers as "the
 * channel moves a block, then the CPU runs for DMA_SM1_GPU_GAP cycles", with
 * the gap a constant 9.8. That number is well measured - M39 fitted it against
 * ps1-tests' dma/chopping over all 65 block sizes, 1.13% mean error, and it is
 * what unfroze Parasite Eve II.
 *
 * BUT LOOK AT WHAT THE CPU IS DOING IN THAT TEST. dma/chopping's measured
 * region is, in full:
 *
 *     startDmaTransfer();
 *     waitForChannel(Channel::GPU);     // a tight spin on CHCR bit 24
 *
 * The CPU spins on ONE I/O register and touches RAM not once. So 9.8 is the
 * window an IDLE CPU gets. Nothing anywhere measures the window a BUSY one
 * gets, and this core hands over the same 9.8 either way - a CPU hammering RAM
 * is given exactly as much progress as a CPU spinning in cache.
 *
 * On real hardware those are not the same thing. The DMA controller and the
 * CPU share one path to RAM; a CPU doing back-to-back stores has to arbitrate
 * for it, and a CPU spinning on an I/O read does not.
 *
 * WHY IT MATTERS (M149). Championship Motocross 2001 loses its riders to
 * exactly this. The game starts an 8,192-word CPU->VRAM upload of the rider's
 * texture, checks D2_CHCR three times, finds it busy, defers the rider's
 * PALETTE upload to the DMA-completion handler, and - without waiting -
 * reallocates and MEMSETS the buffer both uploads read from. The texture
 * survives because the DMA stays ahead of the memset; the palette does not,
 * because it sits 20 bytes from the start of the block. The rider is then
 * drawn ~4,300 triangles a frame against 256 zero entries: fully transparent.
 * On hardware the palette is uploaded in time. Sweeping our gap shows the race
 * plainly - gap 2 gives a whole palette, gap 5 gives 204 of 256 entries (a
 * partial, which can only happen if CPU stores and DMA reads interleave inside
 * one transfer), and the measured 9.8 gives none.
 *
 * So the question this asks is not "how long does a transfer take" - chopping
 * answered that - but "how much WORK does the CPU get done while it runs".
 *
 * WHAT IT MEASURES. One identical transfer, three times, changing only what the
 * CPU does while it is in flight:
 *
 *   SPIN    poll CHCR until idle, touching no RAM          (what chopping does)
 *   REGS    a register-only loop, touching no RAM          (CPU busy, bus free)
 *   STORES  back-to-back word stores to RAM                (CPU busy, bus contended)
 *
 * and reports, for each, the transfer's total cycles and how many loop
 * iterations the CPU completed.
 *
 * READ THE ANSWER LIKE THIS.
 *
 *   If STORES shows the same transfer time and proportionally the same work as
 *   REGS, then the CPU's window really is a flat constant, this core's model is
 *   right, and Motocross's palette dies on hardware too - meaning the divergence
 *   is somewhere else entirely and M149 must look again.
 *
 *   If STORES shows a LONGER transfer, or many fewer iterations than REGS, the
 *   window depends on what the CPU is doing, this core over-serves a
 *   store-heavy CPU, and the fix is a contention term rather than a new value
 *   for 9.8 - which must not be retuned, since chopping still has to fit.
 *
 * THE RULE (hwtests/README.md): a test is not an oracle until it has been shown
 * to measure what it claims. Run it HERE first, where the answer is already
 * known from the source: this core charges a flat gap with no contention, so
 * STORES and REGS must report the SAME transfer time and the store count must
 * match the gap divided by the store cost. A console that agrees has agreed
 * with a model; a console that does not has found the hole.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js dma-contention`):
 *   SPIN 14,176, REGS 14,385 (53 iterations), STORES 14,714 (33 iterations), LOADS 17,032 (5 iterations);
 *   a LOADS step 53.22 cycles during the DMA against 22.27 with none. (M149 read 14,022 / 13,659 / 14,091.)
 */
#include <common.h>
#include <psxapi.h>
#include <malloc.h>
#include <dma.hpp>
#include <measure.hpp>
#include <io.h>

using namespace DMA;

/* 128 x 128 x 16bpp = 32,768 bytes = 8,192 words: the same shape and size as
 * the upload Motocross is doing when it loses the palette. */
#define W 128
#define H 128
#define TRANSFER_BYTES (W * H * 2)
#define BLOCK_SIZE 16

/* Work items per turn of the CPU loops. It has to be LARGE: each turn also
 * costs one busy() poll, which is an I/O read and far dearer than an ALU op or
 * a store. At 16 the poll swamped the work and REGS and STORES came out within
 * 12% of each other whatever they were doing - the instrument was measuring its
 * own poll. At 64 the work dominates and the counts still have resolution. */
#define WORK 64

static uint8_t* buffer;

/* A second buffer for the STORES case, so the store loop never writes over the
 * DMA's own source - that would measure corruption, not contention. */
/* VOLATILE, and it matters: without it the store loop writes an array nothing
 * ever reads and the compiler deletes it outright. The first working build did
 * exactly that - WORK went from 16 to 256 and the iteration counts did not move
 * by one, because neither loop existed. */
static volatile uint32_t scratch[256] __attribute__((aligned(16)));

static volatile uint32_t sink;

/* Open a CPU->VRAM transfer exactly as dma/chopping does, so the setup is not
 * a variable between this test and the one that measured 9.8. */
static void beginTransfer(int x, int y) {
    writeGP1(1, 0);            /* reset the command buffer */
    DrawSync(0);               /* and wait for the GPU to be idle */

    CPU2VRAM buf;
    setcode(&buf, 0xA0);       /* CPU -> VRAM */
    setlen(&buf, 3);
    setXY0(&buf, x, y);
    setWH(&buf, W, H);
    DrawPrim(&buf);

    writeGP1(4, 2);            /* DMA direction: CPU -> VRAM */
}

static void armChannel(void) {
    auto addr = MADDR((uint32_t)buffer);
    BCR block = BCR::mode1(BLOCK_SIZE, TRANSFER_BYTES / BLOCK_SIZE / 4);
    /* DPCR's reset value is 07654321h, which leaves every channel's enable bit
     * CLEAR - the first version of this test armed MADR/BCR/CHCR correctly and
     * then spun for ever, because channel 2 was never allowed to run. */
    write32(CONTROL_ADDR, read32(CONTROL_ADDR) | (1u << 11));
    waitForChannel(Channel::GPU);
    write32(baseAddr(Channel::GPU), addr._reg);
    write32(blockAddr(Channel::GPU), block._reg);
}

static uint32_t chcr(void) {
    CHCR c = CHCR::VRAMwrite();
    c.syncMode = CHCR::SyncMode::syncBlockToDmaRequests;
    return c._reg;
}

static int busy(void) {
    return (read32(controlAddr(Channel::GPU)) & (1u << 24)) != 0;
}

/* Each case returns the iteration count through `iters` and the whole
 * transfer's cycle count as its result. The start write is inside the measured
 * region in all three, exactly as chopping has it. */
static uint32_t runSpin(uint32_t* iters) {
    uint32_t n = 0;
    armChannel();
    const uint32_t cycles = measure<MeasureMethod::CpuClock>([]() {
        write32(controlAddr(Channel::GPU), chcr());
        while (busy()) { }
    });
    /* The spin's own count is not comparable with the other two (it is one I/O
     * read per turn, not one unit of work), so it is reported as zero. */
    *iters = n;
    return cycles;
}

static volatile uint32_t g_iters;

static uint32_t runRegs(uint32_t* iters) {
    armChannel();
    g_iters = 0;
    const uint32_t cycles = measure<MeasureMethod::CpuClock>([]() {
        write32(controlAddr(Channel::GPU), chcr());
        uint32_t n = 0, acc = 1;
        /* Register-only work: no load, no store, so the bus stays free. ONE
         * CHEAP ALU OP per turn, to match the store loop turn for turn - the
         * first version used acc*3+1 and its multiplies made REGS slower than
         * STORES, which made the comparison meaningless (541%). */
        do {
            /* the empty asm keeps each add - otherwise this folds to a
             * closed form and the loop vanishes the same way the stores did */
            for (int i = 0; i < WORK; i++) { acc += i; __asm__ volatile("" : "+r"(acc)); }
            n++;
        } while (busy());
        g_iters = n;
        sink = acc;
    });
    *iters = g_iters;
    return cycles;
}

static uint32_t runStores(uint32_t* iters) {
    armChannel();
    g_iters = 0;
    const uint32_t cycles = measure<MeasureMethod::CpuClock>([]() {
        write32(controlAddr(Channel::GPU), chcr());
        uint32_t n = 0;
        /* Back-to-back word stores to RAM - the same thing the game's memset is
         * doing, and the case nothing has ever measured. WORK per turn, matching
         * the REGS loop turn for turn. */
        do {
            for (int i = 0; i < WORK; i++) scratch[i & 255] = n;   /* volatile: every store is real */
            n++;
        } while (busy());
        g_iters = n;
    });
    *iters = g_iters;
    return cycles;
}

/*
 * LOADS (added for NBA Live 97 (Europe), STATUS "NBA Live 97"): the game's own
 * pad-driver delay loop, a counter on the stack read twice and written once per
 * step - lw, lw, sw - while a SyncMode 1 upload runs. On this core a step costs
 * ~81 cycles during the upload and ~22 without it (the first load waits for a
 * block gap, ~47; the second ~28; the store 3), because the capture's census
 * gives the CPU one access per gap. That census was taken from a program that
 * rarely wanted more than one access per gap, so it measured demand, not what
 * a gap can serve. This case asks the console directly.
 *
 * THE EXACT QUESTION (traced on M211, STATUS "NBA Live 97, traced to the
 * access"): each step's store is buffered under a block and drains in the next
 * gap; the step after it LOADS THAT SAME WORD, and a load waits for the drain
 * (no forwarding, Redux #2091). Here the drain takes the whole gap, so the load
 * waits a further block: three block periods a step, ~78 cycles. If the console
 * lets one gap stretch to serve the drain AND the load behind it - its capture
 * does stretch a gap for a read FOLLOWED by a write, 457 times in TUTO3, but
 * never shows the write-then-read order - a step is two periods, ~52-58. NBA
 * Live 97 needs ~71 or less: its /ACK lands 1,445 cycles after the byte and
 * the driver clears I_STAT at the end of its 20-step delay.
 */
static volatile uint32_t loadcell;

static uint32_t loadsBody(uint32_t steps) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t a = loadcell;          /* lw */
        uint32_t b = loadcell;          /* lw */
        loadcell = b - 1 + (a & 0);     /* sw */
        n++;
    }
    return n;
}

static uint32_t runLoads(uint32_t* iters) {
    armChannel();
    g_iters = 0;
    const uint32_t cycles = measure<MeasureMethod::CpuClock>([]() {
        write32(controlAddr(Channel::GPU), chcr());
        uint32_t n = 0;
        do { loadsBody(WORK); n++; } while (busy());
        g_iters = n;
    });
    *iters = g_iters;
    return cycles;
}

static volatile uint32_t g_steps;
static uint32_t loadsIdle(uint32_t steps) {
    g_steps = steps;
    return measure<MeasureMethod::CpuClock>([]() { loadsBody(g_steps); });
}

int main() {
    printf("\ndma/dma-contention\n");
    buffer = (uint8_t*)malloc(TRANSFER_BYTES);
    for (int i = 0; i < TRANSFER_BYTES; i++) buffer[i] = (uint8_t)(i * 7 + 1);

    printf("One %dx%d CPU->VRAM transfer (%d words, SyncMode 1, blockSize %d),\n",
           W, H, TRANSFER_BYTES / 4, BLOCK_SIZE);
    printf("run three times, changing only what the CPU does while it is in flight.\n\n");

    uint32_t si = 0, ri = 0, ti = 0;

    beginTransfer(0, 0);
    const uint32_t spin = runSpin(&si);
    beginTransfer(0, 128);
    const uint32_t regs = runRegs(&ri);
    beginTransfer(0, 256);
    const uint32_t stores = runStores(&ti);
    uint32_t li = 0;
    beginTransfer(128, 0);
    const uint32_t loads = runLoads(&li);
    const uint32_t idleSteps = 4096;
    const uint32_t idle = loadsIdle(idleSteps);

    printf("  SPIN    %6d cycles   (CPU polls CHCR, touches no RAM - what dma/chopping does)\n", spin);
    printf("  REGS    %6d cycles   %6d iterations   (CPU busy, %d ALU ops per iteration)\n", regs, ri, WORK);
    printf("  STORES  %6d cycles   %6d iterations   (CPU busy, %d word stores per iteration)\n", stores, ti, WORK);
    printf("\n");

    printf("  LOADS   %6d cycles   %6d iterations   (CPU busy, %d lw/lw/sw steps per iteration)\n", loads, li, WORK);
    if (li > 0) printf("  LOADS step during DMA:  %d.%02d cycles  (%d steps)\n", (int)(loads / (li * WORK)), (int)((100 * loads / (li * WORK)) % 100), li * WORK);
    printf("  LOADS step with NO DMA: %d.%02d cycles  (%d steps, %d cycles)\n\n", (int)(idle / idleSteps), (int)((100 * idle / idleSteps) % 100), idleSteps, idle);

    if (ri > 0) {
        const int pct = (int)((100 * (int)ti) / (int)ri);
        printf("  STORES did %d%% of the iterations REGS did.\n", pct);
    }
    printf("  transfer time, STORES vs SPIN: %+d cycles\n", (int)stores - (int)spin);
    printf("  transfer time, REGS   vs SPIN: %+d cycles\n", (int)regs - (int)spin);
    printf("\n");
    printf("  A FLAT WINDOW (this core's model) predicts all three transfer times\n");
    printf("  equal and STORES/REGS explained only by the cost of a store.\n");
    printf("  CONTENTION predicts STORES slower, fewer iterations, or both.\n");
    printf("\nDone.\n");

    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
