/*
 * gpu/fifo-burst - does the CPU wait when it writes GP0 faster than the GPU
 * takes the words, or does it overrun the GPU's command FIFO?
 *
 * WHY (M223, docs/GPU-FIFO-RESEARCH.md section 11; ROAD B). This core's GP0
 * FIFO is 16 words, and as landed a CPU that writes into a full FIFO is NOT
 * made to wait: every hardware source the model was built from says a
 * console's CPU overruns it (psx-spx "GPU FIFO / Overrun", rixnobis on psx-spx
 * issue #113, Sony's developer board). PeterLemon's per-instruction tests say
 * the opposite: their verdicts are ~1,000 unpaced GP0 words behind a fill, and
 * his screenshots show every one of them drawn - so peterlemontest.js runs his
 * machine, GPU_FIFO_OPTS.cpuStall = 'all', a CPU that waits. Which machine his
 * screenshots came from is the open question, and this is the console run
 * that answers it.
 *
 * WHAT IT DOES. 64 one-pixel dots, GP0(68h) - two words each, each a colour no
 * other dot has - are written to GP0 as fast as the CPU can store them: 128
 * constant stores, unrolled, a load-immediate and a store per word. Three
 * scenes, interrupts off, Timer 2 at the system clock / 8:
 *   IDLE  - the dots alone, to an idle GPU. The control: the burst's own time
 *           (the floor), and every dot drawn.
 *   FILL  - the dots straight after a 320x240 GP0(02h) fill, the slow command
 *           the FIFO fills up behind.
 *   COPY  - the dots straight after a 256x128 GP0(80h) VRAM-to-VRAM copy, and
 *           the copy's destination read back, so "the copy ran" is checked and
 *           not assumed (rixnobis's point about the tests that came before).
 *   PACED - the control: the dots again, each written only once GPUSTAT.26 says
 *           the GPU is ready. Every machine draws all 64, or the run says nothing.
 * For each: the burst's time (first store to last), the time to the GPU idle,
 * the dots found at their places in their colours, and which ones are missing.
 *
 * READ IT LIKE THIS. A CPU that WAITS writes FILL's burst in about the fill's
 * time and every dot is drawn, everywhere. A CPU that OVERRUNS writes it at
 * IDLE's speed and the dots behind the first ~16 words are lost. IDLE is a
 * measurement too, not a control: whether an idle GPU keeps up with an unpaced
 * CPU at all. (It was written as the control first; this core's run lost 11
 * dots there, which is a prediction, not a broken test - so PACED is the
 * control.)
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js fifo-burst`), which OVERRUNS:
 *   PACED 2,024 cycles 64/64; IDLE 640, 53/64 (dots 19-29 lost); FILL 352, 8/64
 *   (the FIFO's 16 words), to idle 6,752; COPY 344, 0/64, to idle 44,440, the
 *   copy checked at 16 of 16 points.
 * The same core as PeterLemon's machine, GPU_FIFO_OPTS.cpuStall = 'all' (a CPU
 * that WAITS): PACED 64/64; IDLE 688, 64/64; FILL 7,160, 64/64; COPY 45,016,
 * 64/64. So the test tells the two apart, and a console answers which one it is.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define GP0 (*(volatile uint32_t*)0x1F801810)
#define GPUSTAT (*(volatile uint32_t*)0x1F801814)
#define DOTS 64
#define DOT_X 400
#define DOT_Y 300
#define SENTINEL 0x7C1F          /* 15-bit magenta: no dot is this colour */

/* Dot i's colour: red i & 31, green 1 or 2, blue 16 - distinct, never the sentinel. */
#define RGB(i) ((uint32_t)((((i) & 31) << 3) | ((((i) >> 5) + 1) << 11) | (0x80 << 16)))
#define PIX(i) ((uint16_t)(((i) & 31) | ((((i) >> 5) + 1) << 5) | (16 << 10)))

/* The burst: 128 stores of constants, nothing between them but the constant's
 * own load-immediate. The macros expand to straight-line code. */
#define D(i) GP0 = 0x68000000 | RGB(i); GP0 = (DOT_Y << 16) | (DOT_X + (i));
#define D8(i) D(i) D(i + 1) D(i + 2) D(i + 3) D(i + 4) D(i + 5) D(i + 6) D(i + 7)
static void __attribute__((noinline)) burst() {
    D8(0) D8(8) D8(16) D8(24) D8(32) D8(40) D8(48) D8(56)
}

/* The control: the same dots, each written only once GPUSTAT.26 says the GPU is ready for a command word. */
static void __attribute__((noinline)) paced() {
    for (int i = 0; i < DOTS; i++) {
        while (!(GPUSTAT & (1 << 26)));
        GP0 = 0x68000000 | RGB(i);
        GP0 = (DOT_Y << 16) | (DOT_X + i);
    }
}

static uint16_t line[256 * 16] __attribute__((aligned(4)));

static void clearDots() {
    for (int i = 0; i < 256 * 16; i++) line[i] = SENTINEL;
    vramWriteDMA(DOT_X - 8, DOT_Y, 256, 1, line);
    DrawSync(0);
}

static int countDots(uint32_t* missing) {
    int n = 0;
    missing[0] = missing[1] = 0;
    for (int i = 0; i < DOTS; i++) {
        if ((vramGet(DOT_X + i, DOT_Y) & 0xFFFF) == PIX(i)) n++;
        else missing[i >> 5] |= 1u << (i & 31);
    }
    return n;
}

static void waitIdle() {
    DrawSync(0);
    while (!(GPUSTAT & (1 << 26)));
}

struct Result { uint32_t burst, total; int dots; uint32_t missing[2]; };

/* `before` writes the slow command (or nothing); the dots follow at once, by `dots`. */
static Result scene(void (*before)(), void (*dots)()) {
    Result r;
    clearDots();
    waitIdle();
    resetTimer(2);
    if (before) before();
    const uint16_t t0 = readTimer(2);
    dots();
    const uint16_t t1 = readTimer(2);
    while (!(GPUSTAT & (1 << 26)));
    const uint16_t t2 = readTimer(2);
    r.burst = (uint16_t)(t1 - t0) * 8;
    r.total = (uint16_t)(t2 - t0) * 8;
    r.dots = countDots(r.missing);
    return r;
}

static void fill() {
    GP0 = 0x02102030;               /* GP0(02h), a dark colour */
    GP0 = 0;                        /* 0,0 */
    GP0 = (240 << 16) | 320;        /* 320x240 */
}

static void copy() {
    GP0 = 0x80000000;               /* VRAM to VRAM */
    GP0 = (256 << 16) | 0;          /* from 0,256 */
    GP0 = (256 << 16) | 512;        /* to 512,256 */
    GP0 = (128 << 16) | 256;        /* 256x128 */
}

static void report(const char* name, const Result& r) {
    printf("  %-5s burst %6d cycles, to idle %7d cycles, dots %2d/64", name, (int)r.burst, (int)r.total, r.dots);
    if (r.dots < DOTS) printf("  missing %08x %08x", (unsigned)r.missing[1], (unsigned)r.missing[0]);
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/fifo-burst\n");
    printf("64 dots (128 GP0 words) written unpaced: does the CPU wait on a full FIFO, or overrun it?\n");
    printf("Timer 2 at sysclk/8, interrupts off. There are no assertions - compare against your emulator.\n\n");

    /* The whole of VRAM is drawable, offset 0, drawing to the displayed area allowed. */
    writeGP0(0xE1, 1 << 10);
    writeGP0(0xE3, 0);
    writeGP0(0xE4, 1023 | (511 << 10));
    writeGP0(0xE5, 0);

    /* The copy's source: a pattern at 0,256, and its destination cleared, so "the copy ran" can be read back. */
    for (int y = 0; y < 128; y += 16) {
        for (int i = 0; i < 256 * 16; i++) line[i] = (uint16_t)(((i & 255) ^ (y + (i >> 8))) & 0x7FFF) | 1;
        vramWriteDMA(0, 256 + y, 256, 16, line);
    }
    for (int i = 0; i < 256 * 16; i++) line[i] = 0;
    for (int y = 0; y < 128; y += 16) vramWriteDMA(512, 256 + y, 256, 16, line);
    DrawSync(0);

    const uint16_t prevT2 = initTimer(2, 2);     /* clock source 2: the system clock / 8 */
    EnterCriticalSection();
    const Result control = scene(0, paced);
    const Result idle = scene(0, burst);
    const Result afterFill = scene(fill, burst);
    const Result afterCopy = scene(copy, burst);
    ExitCriticalSection();
    restoreTimer(2, prevT2);

    report("PACED", control);
    report("IDLE", idle);
    report("FILL", afterFill);
    report("COPY", afterCopy);
    int copied = 0;
    for (int k = 0; k < 16; k++) {
        const int x = (k * 37) & 255, y = (k * 11) & 127;
        if ((vramGet(512 + x, 256 + y) & 0xFFFF) == (vramGet(x, 256 + y) & 0xFFFF)) copied++;
    }
    printf("  the copy's destination matches its source at %d of 16 points%s\n", copied, copied == 16 ? "" : "  *** THE COPY DID NOT RUN ***");
    if (control.dots != DOTS) printf("  *** PACED lost dots: the control failed, this run says nothing ***\n");
    printf("\n  A CPU that waits: FILL's burst about the fill's time, 64/64 everywhere.\n");
    printf("  A CPU that overruns: FILL's burst about IDLE's, dots lost behind the first ~16 words.\n");
    printf("  IDLE says whether an idle GPU keeps up with an unpaced CPU at all.\n");
    printf("\ngpu/fifo-burst: Done.\n");
    for (;;) VSync(0);
}
