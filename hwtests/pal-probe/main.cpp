/*
 * timer/pal-probe - does this console keep its video timing, VBlank and Timer 1's hblanks when switched to PAL?
 *
 * WHY (2026-10-26, the SCPH-5000's first day). pal-dots froze the SCPH-5000 (Japan, BIOS 2.2 J, the 1001's drive
 * chips beside the new GPU) twice, the second time with nothing plugged in during it, and printed nothing either time;
 * every other console here ran it (1001, 5501, 7001, 7501, 7502, 9001 - the 5501 and 7001 with the new GPU's next revision,
 * CXD8561BQ: psx-spx has the CXD8561Q on LATE-PU-8 boards only, the 5000's).
 * pal-dots waits for Timer 1 (hblanks) to step with no limit, interrupts off: a timer that stops, or a video timing that
 * stops making hblanks, holds it for ever. The idea: this board cannot be switched to PAL. This asks, with EVERY wait
 * bounded, so it always ends and prints.
 *
 * WHAT IT DOES. Interrupts off. For NTSC then PAL, each width (320 first): GP1(08h) sets the mode, Timer 1 counts hblanks
 * free-running; then (1) does Timer 1 step at all within ~3,000 polls (~20 lines), (2) does VBlank come (I_STAT bit 0,
 * acknowledged first) within 1.5M polls (~40 frames), (3) Timer 1's count from that VBlank to the next - the lines a
 * frame (NTSC 263, PAL 314). GPUSTAT after. Then NTSC 320 back.
 *
 * READ IT LIKE THIS. "T1 0 steps" = Timer 1 stopped; "vblank NONE" = the GPU stopped its frame; lines 263 / 314 = the
 * timing switched as it should. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define I_STAT   (*(volatile uint32_t*)0x1F801070)
#define GP1R     (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT  (*(volatile uint32_t*)0x1F801814)

#define BOUND 1500000

struct Width { const char* name; uint32_t bits; };
static const Width WIDTHS[] = {
    { "320", 0x01 }, { "256", 0x00 }, { "368", 0x40 }, { "512", 0x02 }, { "640", 0x03 },
};

struct Row { uint32_t t1Steps, vbPolls, vb2Polls, lines, stat; };
static Row rows[2][5];

/* polls until VBlank's I_STAT bit, acknowledged first; BOUND if none */
static uint32_t waitVblank() {
    I_STAT = ~1u;
    uint32_t n = 0;
    while (!(I_STAT & 1) && n < BOUND) n++;
    return n;
}

static void probe(int pal, int w) {
    Row& r = rows[pal][w];
    GP1R = 0x08000000u | (pal ? 0x08u : 0u) | WIDTHS[w].bits;
    write16(T1_MODE, 0x0100);                          /* hblanks, free-running */
    const uint16_t a = read16(T1_COUNT);
    uint32_t steps = 0;
    uint16_t last = a;
    for (int i = 0; i < 3000; i++) { const uint16_t v = read16(T1_COUNT); if (v != last) { steps++; last = v; } }
    r.t1Steps = steps;
    r.vbPolls = waitVblank();                          /* the first VBlank in the new timing (or none) */
    r.vbPolls = waitVblank();                          /* a whole frame in it */
    const uint16_t t0 = read16(T1_COUNT);
    r.vb2Polls = waitVblank();
    const uint16_t t1 = read16(T1_COUNT);
    r.lines = (uint16_t)(t1 - t0);
    r.stat = GPUSTAT;
}

int main() {
    initVideo(320, 240);
    printf("\ntimer/pal-probe\n");
    printf("NTSC then PAL, each width: Timer 1 (hblanks) stepping, VBlank coming, lines a frame - every wait bounded.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE);
    for (int pal = 0; pal < 2; pal++)
        for (int w = 0; w < 5; w++) probe(pal, w);
    GP1R = 0x08000001u;                                /* back to NTSC 320 */
    write16(T1_MODE, m1);
    I_STAT = ~1u;
    ExitCriticalSection();
    for (int pal = 0; pal < 2; pal++)
        for (int w = 0; w < 5; w++) {
            const Row& r = rows[pal][w];
            printf("%s %s  T1 %u steps in 3000 polls  vblank %s (%u polls)  next vblank %s  lines a frame %u  GPUSTAT %08x\n",
                   pal ? "PAL " : "NTSC", WIDTHS[w].name, (unsigned)r.t1Steps,
                   r.vbPolls >= BOUND ? "NONE" : "yes", (unsigned)r.vbPolls,
                   r.vb2Polls >= BOUND ? "NONE" : "yes", (unsigned)r.lines, (unsigned)r.stat);
        }
    printf("\ntimer/pal-probe: Done.\n");
    for (;;) VSync(0);
}
