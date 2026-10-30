/*
 * gpu/gpu-a0-size - how many data words does a GP0(A0h) CPU-to-VRAM copy take,
 * for the sizes nobody measured - and does the GPU go idle after a copy of size
 * FFFFFFFFh and one data word, as Tenga Seiha needs?
 *
 * psx-spx "Masking for COPY Commands parameters": Xsiz=((Xsiz-1) AND 3FFh)+1,
 * Ysiz=((Ysiz-1) AND 1FFh)+1, "the only special case is that Size=0 is handled
 * as Size=max". So FFFFh by FFFFh is 1023 x 511 = 522,753 halfwords.
 *
 * Tenga Seiha (Japan) calls ResetGraph(3) and never ResetGraph(0), so libgpu's
 * VRAM size (800D5544h/800D5546h) stays 0; LoadImage clips a {256,511,256,1}
 * rectangle to min(w, 0 - 1) = -1 and sends GP0(A0h), 01FF0100h, FFFFFFFFh,
 * one data word 03000000h - then DrawSync, which waits for GPUSTAT.26 (STATUS
 * "Phase E"). With the copy at 1023 x 511 the GPU waits for pixels, DrawSync
 * times out after 240 VSyncs (five times at boot, ~20 s), and since M231 a CD
 * sector left pending through those waits is overwritten (nocash's queueing
 * logs) and the game never reaches its title. DuckStation SKIPS any copy of
 * size FFFFFFFFh ("It's unclear what actually happens, I need to write another
 * test") and the game reaches its title by f2400. This is that test.
 *
 * For each size word: DrawSync, then GP0(A0h), position (256, 500), the size,
 * ONE data word - and GPUSTAT's bits 26/27/28 sampled right away and after
 * about one and five frames (A = "after one word"). Then words are fed one at a
 * time, each after bit 28 (room in the FIFO), until bit 26 (idle) reads 1 or
 * 270,000 words have gone: "idle after N words" is the transfer's real size
 * in words (N = 1 means it took only the first). GP1(01h) resets the command
 * buffer after each case. Interrupts off throughout.
 *
 * CONTROLS: 2x1 (1 word) and 256x1 (128 words) must read exactly that, and
 * size 0 psx-spx's max, 1024x512 = 262,144.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.263 (2026-09-28, `node hwtests/run.js gpu-a0-size`) - psx-spx's
 * masking, word for word:
 *     2x1 (control)       idle after 1 word           (bits 101 at once)
 *     256x1 (control)     idle after 128 words        (bits 001: busy, then idle)
 *     0 (max)             idle after 262,144 words    (1024 x 512)
 *     FFFFFFFF (Tenga)    idle after 261,377 words    (1023 x 511) - busy after the one word
 *     FFFF wide, 1 high   idle after 512 words        (1023 x 1)
 *     1 wide, FFFF high   idle after 256 words        (1 x 511)
 *     8000 x 8000         idle after 262,144 words    (1024 x 512)
 *     0400 x 0200         idle after 262,144 words
 * CONSOLE DAY (the SCPH-5501, 7501 and 9001): every size as this core counts it, the masking psx-spx's; but the
 * read straight after the first word 000 on every row - the GPU still taking the words out of its FIFO - which
 * hwtests/gpu-a0-ready then timed. ON THIS CORE SINCE v0.377.21 (GPU_DECODE_FIRST) the consoles' rows exactly.
 * DuckStation's special case would read FFFFFFFF "idle after 1 word", bits 101
 * at once, and everything else as here. A console decides: idle at once, and
 * Tenga Seiha's boot is DuckStation's; 261,377 words, and it is this core's
 * (its DrawSync timeouts are then the game's own, and so is what follows).
 * THE OTHER REFERENCES (REFERENCES 66): mednafen (gpu.cpp Command_FBWrite) and
 * MiSTer (gpu_cpu2vram.vhd) both mask the size to 1023 x 511 as psx-spx does;
 * DuckStation alone skips it.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>

#define GP0R    (*(volatile uint32_t*)0x1F801810)
#define GPUSTAT (*(volatile uint32_t*)0x1F801814)
#define GP1R    (*(volatile uint32_t*)0x1F801814)

static void settle(int n) { for (volatile int i = 0; i < n; i++) { } }

static uint32_t bits(uint32_t s) { return ((s >> 26) & 1) * 100 + ((s >> 27) & 1) * 10 + ((s >> 28) & 1); }

struct Case { const char* name; uint32_t size; };
static const Case CASES[] = {
    { "2x1 (control)",        0x00010002 },
    { "256x1 (control)",      0x00010100 },
    { "0 (max, 1024x512)",    0x00000000 },
    { "FFFFFFFF (Tenga)",     0xFFFFFFFF },
    { "FFFF wide, 1 high",    0x0001FFFF },
    { "1 wide, FFFF high",    0xFFFF0001 },
    { "8000 x 8000",          0x80008000 },
    { "0400 x 0200",          0x02000400 },
};

static void runCase(const Case& c) {
    DrawSync(0);
    GP1R = 0x01000000;                 /* reset the command buffer */
    settle(200);
    GP0R = 0xA0000000;
    GP0R = (500u << 16) | 256u;
    GP0R = c.size;
    GP0R = 0x03000000;                 /* the one data word Tenga Seiha sends */
    uint32_t s0 = GPUSTAT;
    settle(20000);                     /* ~1 frame of loop at 33 MHz */
    uint32_t s1 = GPUSTAT;
    settle(100000);
    uint32_t s5 = GPUSTAT;
    uint32_t n = 1, idleAt = 0;
    if (s5 & (1u << 26)) idleAt = 1;
    while (!idleAt && n < 270000) {
        int guard = 0;
        while (!(GPUSTAT & (1u << 28)) && guard++ < 100000) { }
        GP0R = 0x7C1F7C1F;
        n++;
        settle(4);
        if (GPUSTAT & (1u << 26)) idleAt = n;
    }
    printf("  %-20s size %08x | after 1 word: bits 26/27/28 %03u, %03u, %03u | ",
           c.name, (unsigned)c.size, (unsigned)bits(s0), (unsigned)bits(s1), (unsigned)bits(s5));
    if (idleAt) printf("idle after %u words\n", (unsigned)idleAt);
    else printf("still busy after %u words\n", (unsigned)n);
    GP1R = 0x01000000;
    settle(200);
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-a0-size\n");
    printf("GP0(A0h) at (256,500): the size word, one data word, then words until GPUSTAT.26 (idle).\n");
    printf("Bits shown as 26/27/28, e.g. 101 = idle, no read data, FIFO ready. No assertions.\n\n");
    EnterCriticalSection();
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) runCase(CASES[i]);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) VSync(0);
}
