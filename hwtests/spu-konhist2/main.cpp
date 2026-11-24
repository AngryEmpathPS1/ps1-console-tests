/*
 * spu/spu-konhist2 - spu-konhist with the voice at OTHER PITCHES: is the carried history still the decoder ring's slots 11 and
 * 10, and does the old stream still stop three samples before the restart? (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-konhist on six consoles, pitch 1000h: a key-on carries the old stream's samples 11 and 10 of each twelve
 * counted from that stream's key-on, the old decoder stopped three places before this emulator stopped it. At 1000h a place is
 * an output sample and a halfword is four of them: whether the three are output samples or a halfword's worth of source, and
 * whether the twelve hold when the decoder reads faster or slower, needs other pitches.
 *
 * WHAT IT DOES. spu-konhist's blocks, stamps and trial (the old loop keyed on, released 300 samples on, the start address moved
 * to the filter-1 block of zero nibbles, keyed on again 904 + j samples after SPUSTAT bit 11's turn, j = 0-63; the capture ring
 * read back) with the voice's pitch 0800h (set 1), 2000h (set 2) or 3000h (set 3), the same for the old stream and the new.
 *   H set j dt on : v0 ... v35     as spu-konhist
 * experiments/spu_konhist.js reads it. 192 trials. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-konhist2 --frames=12000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static void waitUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 400000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { printf("T %x\n", (unsigned)read32(0x1F8010C8)); write32(0x1F8010C8, 0); }
    if (toSpu) idle(100000);
    spuMode(0);
}

/* The old loop: 112 samples, every pair of neighbours (the last and the first too) different. */
static const int8_t OLD[112] = {
    -7, -4, -7, 7, -5, -6, 3, -7, -3, 7, 1, -6, 6, 0, -7, -5, 5, -1, -5, 7, 4, 5, 4, -3, 2, -1, -2, 2,
    0, 3, 6, 2, 1, -3, 4, 0, 2, 3, -1, 4, 7, -2, 4, -7, 5, -4, 7, -4, 6, 4, -6, 1, 0, 5, -6, 4,
    6, -7, -2, 7, 6, -1, -6, -3, -7, 3, 7, -7, 2, -3, 1, 7, -3, -5, -3, -4, 2, 5, 1, -5, -4, 1, -7, 6,
    -3, -2, 5, -5, 4, -5, 3, -4, -2, -1, 1, -1, 7, -6, -2, 3, 0, -1, 6, 3, -5, -2, -6, -7, 4, 2, -6, -1 };

static uint32_t blocks[16] __attribute__((aligned(16)));
static uint32_t ringw[256] __attribute__((aligned(16)));

static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
}
/* SPUSTAT bit 11's next turn: the Timer 2 stamp of the read behind the one that saw it (0: none in 600,000 cycles). */
static uint32_t edge() {
    const uint32_t t0 = now();
    const int s0 = (read16(SPU(0x1AE)) >> 11) & 1;
    for (;;) {
        const int s = (read16(SPU(0x1AE)) >> 11) & 1;
        const uint32_t tb = now();
        if (s != s0) return tb;
        if (tb - t0 > 600000u) return 0;
    }
}
extern "C" { uint16_t kon_t2(void); }
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".globl kon_t2\n.type kon_t2,@function\nkon_t2:\n"
    "  lui $t0, 0x1f80\n  li $t2, 2\n"
    "  lhu $v0, 0x1120($t0)\n"
    "  sh $t2, 0x1d88($t0)\n"
    "  jr $ra\n  nop\n"
    ".set pop\n");

static void trial(int set, int j) {
    allOff();
    write16(SPU(0x010), 0); write16(SPU(0x012), 0);
    write16(SPU(0x014), (uint16_t)(set == 1 ? 0x0800 : set == 2 ? 0x2000 : 0x3000));
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
    const uint32_t tE = edge();
    if (!tE) { printf("H %d %d timeout\n", set, j); return; }
    waitUntil(tE + 300u + 4u * 768u);
    write16(SPU(0x188), 0x0002);                                    /* the old loop runs */
    waitUntil(tE + 300u + 304u * 768u);
    write16(SPU(0x18C), 0x0002);                                    /* released: it goes on decoding */
    write16(SPU(0x016), 0x2000 / 8);
    waitUntil(tE + 300u + (904u + (uint32_t)j) * 768u);
    const uint32_t tk = stamp(kon_t2());
    waitUntil(tk + 300u * 768u);
    spuDma(0x800, ringw, 256, false);
    const int16_t* r = (const int16_t*)ringw;
    int on = -1;
    for (int i = 0; i < 512 && on < 0; i++) {
        if (!r[i]) continue;
        bool quiet = true;
        for (int k = 1; k <= 100 && quiet; k++) if (r[(i - k) & 511]) quiet = false;
        if (quiet) on = i;
    }
    now();
    printf("H %d %d %u %d :", set, j, (unsigned)(tk - tE), on);
    for (int k = -2; k < 34; k++) { printf(" %04x", (unsigned)(uint16_t)r[((on < 0 ? 0 : on) + k) & 511]); now(); }
    printf("\n");
    now();
}

int main() {
    printf("\nspu/spu-konhist2\n");
    printf("What a key-on carries from the voice's old stream, by where that stream was: H set j dt on : 36 ring samples. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);                    /* the reverb's area: the top 16 bytes */
    write16(SPU(0x1AC), 0x0004);
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);

    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) b[i] = 0;
    for (int blk = 0; blk < 4; blk++) {
        b[16 * blk] = 0x00;                         /* filter 0, shift 0: a sample is its nibble << 12 */
        b[16 * blk + 1] = blk == 0 ? 0x04 : blk == 3 ? 0x03 : 0x00;
        for (int i = 0; i < 28; i++) b[16 * blk + 2 + (i >> 1)] |= (uint8_t)((OLD[28 * blk + i] & 15) << ((i & 1) * 4));
    }
    spuDma(0x1000, blocks, 16, true);
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[0] = 0x1C; b[1] = 0x07;                       /* filter 1, every nibble 0, looping on itself */
    spuDma(0x2000, blocks, 16, true);
    b[0] = 0x2C;                                    /* filter 2 */
    spuDma(0x2100, blocks, 16, true);

    for (int set = 1; set <= 3; set++) for (int j = 0; j < 64; j++) trial(set, j);
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-konhist2: Done.\n");
    for (;;) VSync(0);
}
