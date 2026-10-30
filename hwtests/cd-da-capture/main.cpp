/*
 * cdrom/cd-da-capture - CD audio through the SPU's capture ring, at two drive volumes (2026-10-06).
 *
 * WHY. hwtests/cd-xa found on all three consoles that an XA sector reaches the CD capture ring through a x7FFFh/8000h
 * step - every positive sample one below the resampler's sum, every negative equal - and the SCPH-7501 and 9001 placed
 * it AFTER the drive's volume matrix ((s * v) >> 7, then the step; the 7Fh row rules out folding it into the
 * multiplier). The matrix also carries CD-DA. Whether CD-DA goes through the same step decides where it belongs in this
 * core (CDROM.audioFrame, for both, or the XA path alone), and cd-xa cannot say: its disc has no audio track.
 *
 * WHAT IT DOES. Needs a pressed disc with CD audio tracks: Moto Racer World Tour (USA), tracks 2-23 music (any disc with
 * a track 2 of audio runs; the analysis needs its image). Interrupts off, the CD's flags polled, Timer 2 at the system
 * clock. Init, Demute, the matrix left to left and right to right at 80h, the SPU on with CD audio in (its output 0).
 * Setloc to track 2's start + 20 s (GetTD), Play. Then three CAPTURES, each the 256 left and 256 right samples of one
 * capture-ring half, read (stably, 220931E1h) as soon as SPUSTAT bit 11 says the half is finished, with GetlocP before
 * and after: 1 s in at matrix 80h, then 40h, then 80h again, half a second apart. Pause.
 *
 * READ IT LIKE THIS. The samples cannot be compared row for row with this emulator's run (where the capture falls on the
 * track is the drive's timing): `node tools/cddaalign.js <the log> <the disc image>` finds each capture's 256 samples in
 * the track's own PCM (near GetlocP's position) and prints, for each, the samples' difference from the PCM under (a) the
 * matrix alone and (b) the matrix then x7FFFh/8000h. CD-DA through the step reads (b) exact; past it, (a).
 *
 * THE RULE: run here first: `node hwtests/run.js cd-da-capture --disc=motoracer --ntsc --settle=600 --frames=3000 --quiet=1000`.
 * HERE AT v0.369 the matrix alone: cddaalign finds every capture exact under (a), 256 of 256 on both channels.
 * THE CONSOLES (2026-10-06): neither - a 25-tap interpolator, ~0.35 of a sample on the 7501 and 9001, mirrored (~0.65) on
 * the 5501, summing 0.925 at 80h. AT v0.375 (M375, src/psx.js CDDA_KERNEL, jointly fitted): scratch_cddafir.js on this
 * core's own capture finds the same taps (0.730 / 0.388 / -0.157, sum 0.925), residual 0.55.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) { if (irqFlag()) { drain(); ackAll(); } } }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
/* Sound RAM to RAM, stable (220931E1h: hwtests/spu-dma, spu-reverb). */
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    for (uint32_t k = 0; (read32(0x1F8010C8) & 0x01000000) && k < 4000000; k++) {}   /* rule 1: bounded */
    setCnt(cnt & ~0x30);
}

static uint8_t rb[16]; static int rn;
/* A command; its first response's bytes in rb (and, with `second`, the second's). */
static uint8_t cmd(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    uint8_t f = 0; const uint32_t t0 = now();
    while (!(f = irqFlag()) && now() - t0 < MS(1000)) {}
    rn = 0; while (hasResp() && rn < 16) rb[rn++] = resp();
    ackAll();
    if (second && f == 3) { const uint32_t t1 = now(); uint8_t g; while (!(g = irqFlag()) && now() - t1 < MS(5000)) {} drain(); ackAll(); }
    return f;
}
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static void setMatrix(uint8_t m) {
    idx(2); *CD2 = m; *CD3 = 0x00;                      /* left to left, left to right */
    idx(3); *CD1 = m; *CD2 = 0x00; *CD3 = 0x20;         /* right to right, right to left; apply */
}

struct Cap { uint8_t mat; uint8_t before[8], after[8]; int half; int16_t L[256], R[256]; };
static Cap caps[3] __attribute__((aligned(16)));
static int16_t bufL[256] __attribute__((aligned(16))), bufR[256] __attribute__((aligned(16)));

static void capture(Cap &C, uint8_t m) {
    C.mat = m;
    cmd(0x11); for (int i = 0; i < 8; i++) C.before[i] = i < rn ? rb[i] : 0;   /* GetlocP */
    const uint16_t b0 = r16(SPUSTAT) & 0x800;
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x800) == b0 && now() - t0 < MS(50)) {}
    /* bit 11 just flipped: the half it LEFT is finished (0 = first half being written) */
    C.half = b0 ? 1 : 0;
    dmaRead(C.half ? 0x200 : 0x000, bufL, 512);
    dmaRead(C.half ? 0x600 : 0x400, bufR, 512);
    for (int i = 0; i < 256; i++) { C.L[i] = bufL[i]; C.R[i] = bufR[i]; }
    cmd(0x11); for (int i = 0; i < 8; i++) C.after[i] = i < rn ? rb[i] : 0;
}

int main() {
    printf("\ncdrom/cd-da-capture\n");
    printf("CD audio through the capture ring at drive volumes 80h and 40h. Needs a disc with a track 2 of audio.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);    /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    idx(1); *CD2 = 0; ackAll(); drain();

    cmd(0x0A, true);                                    /* Init */
    cmd(0x0C);                                          /* Demute */
    setMatrix(0x80);
    w16(0x1AC, 0x0004);
    setCnt(0xC001);                                     /* SPU on, unmuted, CD audio in */
    w16(0x180, 0); w16(0x182, 0);                       /* output volume 0 */
    w16(0x1B0, 0x7FFF); w16(0x1B2, 0x7FFF);             /* CD input volume (the ring is before it: cd-xa) */

    cmd(0x13);                                          /* GetTN */
    const int first = rn > 1 ? unbcd(rb[1]) : 0, last = rn > 2 ? unbcd(rb[2]) : 0;
    param(0x02); cmd(0x14);                             /* GetTD track 2 */
    const int mm = rn > 1 ? unbcd(rb[1]) : 0, ss = rn > 2 ? unbcd(rb[2]) : 0;
    bool ok = last >= 2;
    if (ok) {
        int s = ss + 20, m = mm + s / 60; s %= 60;
        param(bcd(m)); param(bcd(s)); param(0x00); cmd(0x02);   /* Setloc */
        cmd(0x03);                                      /* Play */
        idle(MS(1000));
        capture(caps[0], 0x80);
        setMatrix(0x40); idle(MS(500));
        capture(caps[1], 0x40);
        setMatrix(0x80); idle(MS(500));
        capture(caps[2], 0x80);
        cmd(0x09, true);                                /* Pause */
    }
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("disc: tracks %d-%d, track 2 at %02d:%02d\n", first, last, mm, ss);
    if (!ok) printf("NO TRACK 2 - put in a disc with CD audio (Moto Racer World Tour) and run again.\n");
    else for (int c = 0; c < 3; c++) {
        const Cap &C = caps[c];
        printf("\nCAPTURE %d, drive volume %02xh, ring half %d; GetlocP before", c + 1, C.mat, C.half);
        for (int i = 0; i < 8; i++) printf(" %02x", C.before[i]);
        printf(", after");
        for (int i = 0; i < 8; i++) printf(" %02x", C.after[i]);
        printf("\n  LEFT:\n");
        for (int i = 0; i < 256; i++) printf("%s %6d%s", (i % 16) ? "" : "  ", C.L[i], (i % 16 == 15) ? "\n" : "");
        printf("  RIGHT:\n");
        for (int i = 0; i < 256; i++) printf("%s %6d%s", (i % 16) ? "" : "  ", C.R[i], (i % 16 == 15) ? "\n" : "");
    }
    printf("\ncdrom/cd-da-capture: Done.\n");
    for (;;) VSync(0);
}
