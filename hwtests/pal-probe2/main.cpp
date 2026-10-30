/*
 * gpu/pal-probe2 - on a board whose PAL oscillator pad is grounded, does the whole GPU stop in PAL mode, or only its video?
 *
 * WHY (2026-10-26, the SCPH-5000). hwtests/pal-probe on the SCPH-5000: in PAL mode no hblank, no VBlank at any width
 * (NTSC fine) - psx-spx: PU-7 .. PU-20 boards switch the GPU between a 53.20 MHz (PAL) and a 53.69 MHz (NTSC) oscillator by
 * the frame rate, and the missing one's pad is "either GNDed or shortcut with the installed oscillator (varies from board
 * to board)". GP1 writes still landed there (GPUSTAT showed each width). If the GPU's one clock is that input, drawing and
 * the command processor stop too; if the video timing has its own, they run on. A model needs to know which.
 *
 * WHAT IT DOES. Interrupts off, every wait bounded (BOUND polls). In NTSC 320, then PAL 320: (1) GP0(E1h) with a fresh
 * texpage - polls until GPUSTAT's low bits show it (the command processor ran); (2) a 64 x 64 fill (GP0(02h)) - polls until
 * GPUSTAT bit 26 (idle) is back (drawing ran); (3) GP1(10h:07h), the GPU's version - what GPUREAD then reads. Then back to
 * NTSC 320 with whatever PAL left pending: polls until idle (a stopped clock resumes). Then the text.
 *
 * READ IT LIKE THIS. "NONE" = BOUND polls without it. PAL rows that read NONE where NTSC's are tens of polls = the GPU's
 * clock stopped; PAL rows like NTSC's = only the video timing stopped. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>

#define GP0R     (*(volatile uint32_t*)0x1F801810)
#define GPUREAD  (*(volatile uint32_t*)0x1F801810)
#define GP1R     (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT  (*(volatile uint32_t*)0x1F801814)

#define BOUND 1500000

struct Row { uint32_t e1Polls, fillPolls, info, statAfter; };
static Row rows[2];
static uint32_t resumePolls, resumeStat;

static uint32_t waitIdle() { uint32_t n = 0; while (!(GPUSTAT & 0x04000000u) && n < BOUND) n++; return n; }

static void probe(int pal, uint32_t page) {
    Row& r = rows[pal];
    GP1R = 0x08000001u | (pal ? 0x08u : 0u);           /* 320 wide */
    for (int i = 0; i < 20000; i++) { (void)GPUSTAT; }  /* a few lines' worth, the mode settled */
    GP0R = 0xE1000000u | page;                         /* texpage bits 0-8 -> GPUSTAT bits 0-8 when executed */
    uint32_t n = 0;
    while ((GPUSTAT & 0x1FFu) != page && n < BOUND) n++;
    r.e1Polls = n;
    GP0R = 0x02203040u;                                /* a 64 x 64 fill at (640, 256) */
    GP0R = (256u << 16) | 640u;
    GP0R = (64u << 16) | 64u;
    for (int i = 0; i < 8; i++) { (void)GPUSTAT; }      /* the busy bit up */
    r.fillPolls = waitIdle();
    GP1R = 0x10000007u;                                /* the GPU's version */
    for (int i = 0; i < 64; i++) { (void)GPUSTAT; }
    r.info = GPUREAD;
    r.statAfter = GPUSTAT;
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/pal-probe2\n");
    printf("NTSC 320 then PAL 320: GP0(E1h) executed?, a 64x64 fill finished?, GP1(10h:07h) answered? - every wait bounded.\n\n");
    EnterCriticalSection();
    probe(0, 0x00Au);
    probe(1, 0x015u);
    GP1R = 0x08000001u;                                /* back to NTSC 320: what PAL left pending */
    resumePolls = waitIdle();
    resumeStat = GPUSTAT;
    ExitCriticalSection();
    for (int pal = 0; pal < 2; pal++) {
        const Row& r = rows[pal];
        printf("%s 320  E1h %s (%u polls)  fill %s (%u polls)  version %08x  GPUSTAT %08x\n", pal ? "PAL " : "NTSC",
               r.e1Polls >= BOUND ? "NONE" : "seen", (unsigned)r.e1Polls, r.fillPolls >= BOUND ? "NONE" : "done",
               (unsigned)r.fillPolls, (unsigned)r.info, (unsigned)r.statAfter);
    }
    printf("back to NTSC 320: idle %s (%u polls)  GPUSTAT %08x\n", resumePolls >= BOUND ? "NONE" : "yes",
           (unsigned)resumePolls, (unsigned)resumeStat);
    printf("\ngpu/pal-probe2: Done.\n");
    for (;;) VSync(0);
}
