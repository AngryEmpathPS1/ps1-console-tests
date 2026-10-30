/*
 * gpu/semi-cost - does a semi-transparent TEXTURED primitive cost more when
 * none of its texels is semi-transparent?
 *
 * WHY (ROAD A10, M236; STATUS "gpu/bandwidth's two builds"). A texel blends
 * only when its STP bit (bit 15) is set, so a semi-transparent primitive over
 * a texture with every STP bit clear draws exactly what the opaque one does.
 * Sony's FRAME.DOC says it still costs 0.3-0.5x more. The two builds of
 * ps1-tests gpu/bandwidth disagree: the shipped exe's console log has a
 * textured quad at 2,530.146 ms semi or not, build-106's has semi costing 14%
 * on a quad and 26% on a rectangle - and both textured whatever was in VRAM at
 * 512,256, so neither says which texels had STP set. This core charges the
 * textured semi rows nothing extra (the fit to the shipped log).
 *
 * WHAT IT DOES. Two 256x256 15-bit textures in VRAM: A at 512,256 with every
 * texel 1234h (STP clear, and not 0000h, which is never drawn), B at 768,256
 * with every texel 9234h (STP set). Each variant draws a 256x240 primitive 100
 * times back to back and times the lot on Timer1 (hblanks, 2,152.6 CPU cycles
 * each on NTSC) from the first word to DrawSync:
 *   SPRT (GP0 64h/66h) opaque and semi, over A and over B;
 *   POLY_FT4 (GP0 2Ch/2Eh) the same;
 *   TILE and POLY_F4, untextured, opaque and semi - the control: an untextured
 *   semi primitive always blends.
 * Semi-transparency mode 0 (B/2 + F/2) throughout, no dither, no mask.
 *
 * READ IT LIKE THIS. semi/A against opaque/A is the question: 100% means a
 * texel that does not blend costs nothing (this core); 130-150% is FRAME.DOC.
 * semi/B against opaque/B is the cost of blending itself, and the controls
 * give it for an untextured primitive.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js semi-cost`): over
 * A and over B alike, SPRT opaque 7,531 hblanks (~162,100 cycles a primitive)
 * and semi 7,607 (101%); POLY_FT4 9,248 and 9,293 (100%). TILE 1,506 against
 * 2,275 semi (151%), POLY_F4 1,532 against 2,305 (150%). This core charges a
 * textured primitive nothing for blending, even when every texel blends (B):
 * the fit to the shipped gpu/bandwidth log. A console that charges B and not A
 * wants the cost counted per blended texel (STATUS, gpu/bandwidth's builds);
 * one that charges A as well is FRAME.DOC.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define N 100

struct Packet { uint32_t tag; uint32_t w[9]; };

static void send(const uint32_t* w, int n) {
    Packet p;
    for (int i = 0; i < n; i++) p.w[i] = w[i];
    setlen(&p, n);
    DrawPrim(&p);
}

/* E1h: texpage X/64, Y/256, semi mode 0, 15-bit, no dither, drawing to the display allowed. */
static uint32_t tpage(int x, int y) { return ((x / 64) & 15) | ((y / 256) << 4) | (2 << 7) | (1 << 10); }

static uint16_t row[256 * 16] __attribute__((aligned(4)));

static void fillTexture(int x, int y, uint16_t v) {
    for (int i = 0; i < 256 * 16; i++) row[i] = v;
    for (int r = 0; r < 256; r += 16) vramWriteDMA(x, y + r, 256, 16, row);
}

static uint32_t timeIt(const uint32_t* w, int n) {
    DrawSync(0);
    resetTimer(1);
    for (int i = 0; i < N; i++) send(w, n);
    DrawSync(0);
    uint32_t v = readTimer(1);
    if (timerDidOverflow(1)) v += 0xFFFF;
    return v;
}

static void report(const char* name, uint32_t lines, uint32_t base) {
    const uint32_t cycles = lines * 21526 / 10 / N;    /* per primitive */
    printf("  %-28s %6d hblanks  ~%7d cycles each", name, lines, cycles);
    if (base) printf("  %3d%% of opaque", lines * 100 / base);
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/semi-cost\n");
    printf("A semi-transparent textured primitive over texels with STP clear: extra cost? (FRAME.DOC: 0.3-0.5x)\n");
    printf("%d primitives of 256x240 a row, Timer1 hblanks. There are no assertions - compare against your emulator.\n\n", N);
    fillTexture(512, 256, 0x1234);     /* A: STP clear */
    fillTexture(768, 256, 0x9234);     /* B: STP set */
    const uint16_t prevT1 = initTimer(1, 1);
    EnterCriticalSection();

    const uint32_t xy0 = 0, xy1 = 256, xy2 = 240 << 16, xy3 = (240 << 16) | 256;
    for (int t = 0; t < 2; t++) {
        const int tx = t ? 768 : 512;
        const char* tn = t ? "B (STP set)" : "A (STP clear)";
        printf("texture %s\n", tn);
        uint32_t e1[1] = { 0xE1000000 | tpage(tx, 256) };
        send(e1, 1);
        const uint32_t sprO[4] = { 0x64808080, xy0, 0x00000000, (240 << 16) | 256 };
        const uint32_t sprS[4] = { 0x66808080, xy0, 0x00000000, (240 << 16) | 256 };
        const uint32_t tp = tpage(tx, 256) << 16;
        const uint32_t ft4O[9] = { 0x2C808080, xy0, 0x00000000, xy1, tp | 0x00FF, xy2, 0xF000, xy3, 0xF0FF };
        const uint32_t ft4S[9] = { 0x2E808080, xy0, 0x00000000, xy1, tp | 0x00FF, xy2, 0xF000, xy3, 0xF0FF };
        const uint32_t a = timeIt(sprO, 4), b = timeIt(sprS, 4);
        report("SPRT opaque", a, 0); report("SPRT semi", b, a);
        const uint32_t c = timeIt(ft4O, 9), d = timeIt(ft4S, 9);
        report("POLY_FT4 opaque", c, 0); report("POLY_FT4 semi", d, c);
    }
    printf("untextured (control: semi always blends)\n");
    const uint32_t tO[3] = { 0x60808080, xy0, (240 << 16) | 256 }, tS[3] = { 0x62808080, xy0, (240 << 16) | 256 };
    const uint32_t f4O[5] = { 0x28808080, xy0, xy1, xy2, xy3 }, f4S[5] = { 0x2A808080, xy0, xy1, xy2, xy3 };
    const uint32_t e = timeIt(tO, 3), f = timeIt(tS, 3);
    report("TILE opaque", e, 0); report("TILE semi", f, e);
    const uint32_t g = timeIt(f4O, 5), h = timeIt(f4S, 5);
    report("POLY_F4 opaque", g, 0); report("POLY_F4 semi", h, g);

    ExitCriticalSection();
    restoreTimer(1, prevT1);
    printf("\nDone.\n");
    for (;;) VSync(0);
}
