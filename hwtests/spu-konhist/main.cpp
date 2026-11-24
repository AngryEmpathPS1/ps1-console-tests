/*
 * spu/spu-konhist - WHAT A KEY-ON CARRIES from the voice's old stream: the decoder's two last samples, by where the old stream
 * was (2026-11-24, the SPU session)
 *
 * WHY. A key-on does not clear the ADPCM decoder's history (M183), and a released voice goes on decoding its old loop
 * (v0.377.194): Redux's silicon goldens adpcm_edge_predictor_1 / 2 / 4 open with the history of wherever the left-running voice
 * was - 141 / 52 / 35 samples differ here, and nothing says which of that loop's places a console is at, because those tests
 * key on from a place their own suite left them in. Heard in games that re-key a looping voice (Discworld Noir's pop before
 * speech). This puts the old stream at a KNOWN place and prints what the new one starts from.
 *
 * WHAT IT DOES. Interrupts off, sound muted. An old loop of four blocks at 1000h, filter 0, shift 0: its 112 samples are
 * nibble << 12 from a sequence whose every pair of neighbours is different, so two decoded samples name the place. A new block
 * at 2000h (filter 1) and one at 2100h (filter 2), every nibble 0, looping on itself: its output is the carried history
 * decaying, nothing else. A trial: every voice off; SPUSTAT bit 11's turn stamped (tE); voice 1 keyed on at 1000h, pitch
 * 1000h, 4 samples after it (300 cycles into a sample: clear of the key-on window); keyed off 300 samples later (the fastest
 * release); the start address moved to the new block; keyed on again 904 + j samples after the turn, j = 0-63 (the old loop
 * is 112 samples: 64 places running); 300 samples on the voice-1 capture ring is read back by DMA.
 *   set 1  the new block filter 1          set 2  filter 2
 *   set 3  filter 1, and SPUCNT bit 15 (SPU enable) clear from sample 400 to sample 600 of the trial: does a disabled SPU's
 *          voice go on decoding? (200 samples = 88 places of the loop further, or not)
 *   H set j dt on : v0 ... v35     dt: the second key-on's Timer 2 read less tE; on: the ring index of the first non-zero sample
 *                                  after silence (-1: none); then 36 ring samples from two before it (hex, as stored)
 * experiments/spu_konhist.js lines a console's rows up against an emulator's: which j of the emulator reads the same row.
 * 192 trials. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-konhist --frames=12000 --quiet=3000`.
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
    write16(SPU(0x014), 0x1000);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
    const uint32_t tE = edge();
    if (!tE) { printf("H %d %d timeout\n", set, j); return; }
    waitUntil(tE + 300u + 4u * 768u);
    write16(SPU(0x188), 0x0002);                                    /* the old loop runs */
    waitUntil(tE + 300u + 304u * 768u);
    write16(SPU(0x18C), 0x0002);                                    /* released: it goes on decoding */
    write16(SPU(0x016), (uint16_t)((set == 2 ? 0x2100 : 0x2000) / 8));
    if (set == 3) {
        waitUntil(tE + 300u + 400u * 768u);
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x8000));             /* the SPU disabled for 200 samples */
        waitUntil(tE + 300u + 600u * 768u);
        write16(SPU(0x1AA), cnt);
    }
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
    printf("\nspu/spu-konhist\n");
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
    printf("\nspu/spu-konhist: Done.\n");
    for (;;) VSync(0);
}
