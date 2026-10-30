/*
 * gpu/gpustat31 - GPUSTAT bit 31, scanline by scanline, as the display's VRAM
 * Y changes parity.
 *
 * WHY (src/psx.js readStatus, KNOWN-OPEN since M131). In 240-line mode psx-spx
 * says bit 31 "changes per scanline"; DuckStation computes (Y + line) & 1, so
 * with the display's VRAM Y odd the two are exact inverses. M131 found every
 * one of 1,719 US discs reading GPUSTAT in 240-line mode with Y odd (7.4
 * million reads), and M133 found the choice changes nothing any of them does:
 * the two signals share every edge. So it is settled for the library and open
 * for the hardware. In 480i the bit is (Y + field) & 1 (DuckStation), constant
 * through a field, and in v-blank it is Y's low bit (AmiDog's VRAM dumps,
 * SCPH-7002, M197).
 *
 * WHAT IT DOES. Timer1 counts hblanks and is reset at each VBlank edge (I_STAT
 * bit 0, polled with interrupts off), so it is the line since v-blank began.
 * Every sample reads Timer1, GPUSTAT, Timer1, and is kept when both timer reads
 * agree. With the display range set to Y1..Y1+240, lines vbl+3 onward are the
 * picture and 2..vbl-3 are v-blank. For each VRAM Y (0, 1, 2, 3, 0 again) in
 * 240-line mode, over 4 frames:
 *   ACTIVE  how often bit 31 equals the Timer1 line's low bit
 *   VBLANK  how often bit 31 is 1
 * then in 480i, per frame: bit 31's value through the picture (0, 1, or m for
 * mixed) beside bit 13 (the interlace field) there, and bit 31 through v-blank.
 *
 * READ IT LIKE THIS. ACTIVE near 100% or 0% for every Y means the bit follows
 * the line alone (psx-spx); flipping between 100% and 0% with Y's parity means
 * (Y + line) & 1 (DuckStation). Which of 100 and 0 an even Y reads is only the
 * offset between the GPU's line count and Timer1's. VBLANK tracks Y's low bit
 * if AmiDog's rule holds, and reads 0 for every Y if psx-spx's "always zero
 * during Vblank" does. In 480i, bit 31 equal to bit 13 (or its inverse) for
 * even Y and the other way round for odd Y is DuckStation's (Y + field) & 1.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js gpustat31`): NTSC,
 * 263 lines, v-blank 23. 240-line mode: ACTIVE 0.0% for every Y and every frame
 * (bit 31 follows the line alone, psx-spx's rule, with the GPU's line count
 * one off Timer1's in parity), VBLANK 0.0% for even Y and 100.0% for odd (Y's
 * low bit, M197), ~31,770 / 2,736 samples. 480i: bit 31 in the picture is the
 * inverse of bit 13 for Y = 0 and 2 and equal to it for Y = 1 and 3 - (Y +
 * field) & 1 - and in v-blank Y's low bit. A console whose ACTIVE flips with
 * Y's parity uses DuckStation's rule in 240-line mode too; M133 found no US
 * disc behaving differently either way.
 *
 * THE CONSOLES (SCPH-5501, 7501, 9001, two runs each): 240-line ACTIVE 5.8% at even Y and 94.1% at odd - flipping with
 * Y, DuckStation's (Y + line) & 1 - VBLANK Y's low bit, and 480i as this core. The 5.8% is the stretch of each line where
 * Timer1 has ticked and bit 31 has not moved yet: ~198 GPU ticks. ON THIS CORE AT v0.377.23 ((Y + line) & 1, and
 * T1_RETRACE: Timer1 200 ticks before the line ends): 5.9% / 94.0%, the rest the consoles'. The samples (~31,480 here,
 * ~32,200 there) are the read loop's speed.
 */
#include <common.h>
#include <psxapi.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define I_STAT 0x1F801070
#define I_MASK 0x1F801074
#define GPUSTAT 0x1F801814

static int pal;
static uint32_t Y1, total, vbl;

static void edge() {
    write32(I_STAT, ~1u);
    while (!(read32(I_STAT) & 1)) { }
    resetTimer(1);
    write32(I_STAT, ~1u);
}

struct Acc { uint32_t act, actAgree, vb, vbOnes; };

/* One frame of samples from the edge just taken. */
static void frame(Acc& a) {
    for (;;) {
        const uint16_t l1 = readTimer(1);
        const uint32_t s = read32(GPUSTAT);
        const uint16_t l2 = readTimer(1);
        if (l1 != l2) continue;
        if (l1 >= total - 3) return;
        const uint32_t b = s >> 31;
        if (l1 >= 2 && l1 + 3 < vbl) { a.vb++; a.vbOnes += b; }
        else if (l1 >= vbl + 3) { a.act++; a.actAgree += (b == (uint32_t)(l1 & 1)); }
    }
}

static void pct(uint32_t n, uint32_t d) { printf("%3d.%d%%", d ? n * 100 / d : 0, d ? (n * 1000 / d) % 10 : 0); }

static void y240(uint32_t y) {
    writeGP1(5, (y & 0x1FF) << 10);
    edge(); edge();
    printf("240  Y=%d  ACTIVE", y);
    Acc all = { 0, 0, 0, 0 };
    for (int f = 0; f < 4; f++) {
        Acc a = { 0, 0, 0, 0 };
        edge(); frame(a);
        printf(" "); pct(a.actAgree, a.act);
        all.act += a.act; all.actAgree += a.actAgree; all.vb += a.vb; all.vbOnes += a.vbOnes;
    }
    printf("  VBLANK "); pct(all.vbOnes, all.vb);
    printf("  (%d / %d samples)\n", all.act, all.vb);
}

static char code(uint32_t ones, uint32_t n) { return n == 0 ? '-' : ones == 0 ? '0' : ones == n ? '1' : 'm'; }

/* 480i. The field a frame shows is not counted from anything this program
 * controls (printf between blocks takes a field or two), so each frame also
 * records GPUSTAT bit 13, the interlace field, and bit 31 is read against it. */
static void y480(uint32_t y) {
    writeGP1(5, (y & 0x1FF) << 10);
    edge(); edge();
    char act[9], fld[9], vb[9];
    for (int f = 0; f < 8; f++) {
        uint32_t n = 0, ones31 = 0, ones13 = 0, nv = 0, vones = 0;
        edge();
        for (;;) {
            const uint16_t l1 = readTimer(1);
            const uint32_t s = read32(GPUSTAT);
            const uint16_t l2 = readTimer(1);
            if (l1 != l2) continue;
            if (l1 >= total - 3) break;
            if (l1 >= 2 && l1 + 3 < vbl) { nv++; vones += s >> 31; }
            else if (l1 >= vbl + 3) { n++; ones31 += s >> 31; ones13 += (s >> 13) & 1; }
        }
        act[f] = code(ones31, n); fld[f] = code(ones13, n); vb[f] = code(vones, nv);
    }
    act[8] = fld[8] = vb[8] = 0;
    printf("480i Y=%d  frames 0-7: bit31 in the picture %s, bit13 (field) %s, bit31 in v-blank %s\n", y, act, fld, vb);
}

int main() {
    printf("\ngpu/gpustat31\n");
    printf("GPUSTAT bit 31 by scanline (Timer1 hblanks since the VBlank edge) as VRAM display Y changes.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    pal = (ReadGPUstat() >> 20) & 1;
    Y1 = pal ? 0x23 : 0x10;
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t prev = initTimer(1, 1);    /* Timer1 counts hblanks, free-running */
    const uint32_t p = pal ? 0x08 : 0x00;
    writeGP1(8, 0x01 | p);                    /* 320 wide, 240 lines */
    writeGP1(7, Y1 | ((Y1 + 240) << 10));
    edge(); edge();
    uint32_t t = 0;
    for (int i = 0; i < 4; i++) { edge(); while (!(read32(I_STAT) & 1)) { } t = readTimer(1); }
    total = t;
    vbl = total - 240;
    printf("video: %s, %d lines a frame, display %d..%d, v-blank %d lines\n\n", pal ? "PAL" : "NTSC", total, Y1, Y1 + 240, vbl);

    static const uint32_t ys[] = { 0, 1, 2, 3, 0 };
    for (unsigned i = 0; i < sizeof(ys) / sizeof(ys[0]); i++) y240(ys[i]);
    printf("\n");
    writeGP1(8, 0x01 | 0x04 | 0x20 | p);      /* 320 wide, 480 lines, interlaced */
    edge(); edge();
    for (unsigned i = 0; i < 4; i++) y480(ys[i]);

    writeGP1(8, 0x01 | p);
    writeGP1(5, 0);
    restoreTimer(1, prev);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) { }
}
