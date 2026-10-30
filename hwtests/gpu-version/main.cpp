/*
 * gpu/gpu-version - every difference psx-spx lists between the old 160-pin GPU and the new 208-pin one that a program
 * can see (2026-10-07).
 *
 * WHY. psx-spx "GPU Versions": launch consoles (EARLY-PU-8 and below: a PU-7 SCPH-1000/1001/1002) carry the CXD8514Q,
 * later ones the CXD8561Q and successors, and the table lists what differs - GPUSTAT bits 13-15, GP1(10h)'s indices
 * and their mirrors, the draw area's 19 or 20 bits, the rectangle flip, GP0(03h), shaded textures cropped to 5:5:5
 * before the multiply ("((color/8)*texel)/2" against "(color*texel)/16"), FillVram at X 0Fh, DMA blocks over 10h words,
 * drawing at Y 200h-3FFh, and a GPU running at "about 60% of the new GPU speed". This core has modelled the new GPU only;
 * CONSOLE_MODELS names the SCPH-1001 placeholder's GPU 'old' and nothing reads it. The three consoles have new
 * GPUs (machine-id: GP1(10h:07h) = 2 on each), so tonight's runs are the NEW column - which psx-spx states from
 * nocash's consoles and nobody here has checked - and the SCPH-1001 (on its way, 2026-10-06), if its board is a PU-7 or
 * an early PU-8, the old one. Every row is either column's prediction; none needs the other GPU to be read.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock. It draws only in VRAM 896-1023 x 496-511, which it saves
 * first and puts back last (DMA, 16-word blocks: what both GPUs take). Drawing area 0,0-1023,511, offset 0, dither off,
 * a 15-bit texture row of 32 texels at 960,496 (texture page 15 / 256: u 0-31, v 240), red = u, green = 31-u, blue 16.
 *   INFO      GPUSTAT as found; GP1(10h) index 07h; then each of 02h 0Ah 12h 1Ah, 03h 0Bh 13h 1Bh, 04h 0Ch, 05h 0Dh
 *             15h 1Dh, 07h 0Fh 17h 1Fh, 08h 18h with GPUREAD first set to 80028001h by a two-word VRAM read (an index that
 *             "returns nothing" leaves 80000000h or 80028001h - gpuinfo-latch - a mirror answers). The texture window
 *             E2h = 000F8421h, area E3h 1,2 / E4h 1022,509, offset E5h (+123h, -55h). And the area latches with Y's
 *             bit 9 set: E3h / E4h = 3FFh,3FFh, read through 03h/04h - 20 bits (new) or 19 (old).
 *   STAT      GPUSTAT.14 with GP1(08h).7 set (the display mode as GPUSTAT shows it, put back at once); GPUSTAT.13 read
 *             8 times with interlace off; GPUSTAT.15 after E1h with bit 11 in 1 MB mode, then after GP1(09h) = 1 (2 MB) and
 *             E1h.11 again, then after GP1(09h) = 0 (psx-spx: in 1 MB mode the bit "simply cannot be set").
 *   SHADE     rows of 32 at 896,497.. : a modulated textured rectangle (GP0(64h)) and a modulated textured quad
 *             (GP0(2Ch)), each with the colour c = 80h 87h 88h 8Fh 47h 4Fh 0Fh FFh on all three channels. New:
 *             (texel * c) >> 7, clamped; old (psx-spx): the colour's 3 low bits gone first.
 *   FILL      GP0(02h) at X 38Fh (896 + 0Fh), Y 508, 32 x 2, over a cleared row of 96: new, X's low 4 bits ignored
 *             (896-927); old, every second pixel 32 to the right (psx-spx's figure).
 *   FLIP      a raw textured rectangle 32 x 1 at 896,510 from u 31 with E1h.12 (X flip): new flips, old does not.
 *   AREA      the drawing area at 896,1008 - 1023,1023 (Y bit 9 set), a flat rectangle at 896,1008: new draws it at row
 *             496 (1008 - 512); old "do never draw pixels at Y=200h..3FFh".
 *   DMA       64 x 4 (128 words) uploaded by DMA2 in blocks of 8, 16, 32 and 64 words and read back: halfwords wrong.
 *             Old: "doesn't work with blksiz>10h".
 *   TIMING    GP0(03h) 0..0Fh before a flat 3 x 128 rectangle (1021,384) and a 64 x 64 one (960,448), inside a second
 *             saved area (960-1023 x 384-495, with the first): cycles from the command's last word to
 *             GPUSTAT.26 and .28 both set (.26 alone comes back early under the FIFO; .28 stays clear for a draw). psx-spx:
 *             3 x 128 about 4x slower at 0Fh, 512 x 128 not slower; the old
 *             GPU takes no GP0(03h). GP0(03h) left at 0.
 *
 * ON THIS CORE AT v0.377.15 (2026-10-07), the new column throughout: INFO 07h 2; 02h 000F8421h, 03h 00000801h, 04h
 * 0007F7FEh, 05h 003D5923h, 07h 2 and their mirrors 12h 13h 15h 17h the same; 0Ah-0Dh, 0Fh, 1Ah-1Dh and 1Fh 80000000h
 * (nothing: bit 31 of 80028001h kept, gpuinfo-latch's rule); the area latches 000FFFFFh (20 bits) - 800FFFFFh since
 * v0.377.18, whose GP1(10h) latch keeps the marker's bits 20-31 as the consoles do (gpuinfo-latch). STAT bit 14 1 with
 * GP1(08h).7; bit 13 1 on all 8 reads with interlace off; bit 15 0 after E1h.11 in 1 MB mode, 1 after GP1(09h) = 1 and
 * E1h.11 again, and still 1 after GP1(09h) = 0 (this core keeps the bit; a console may drop it). SHADE (texel * c) >> 7
 * on both primitives - c 87h takes red 19 to 20 (the crop gives 19), blue 16 at c 88h to 17. FILL 896-927 (X's low 4 bits
 * ignored). FLIP 31 down to 0. AREA row 496 green: drawn at 1008 - 512. DMA 0 wrong at every block size. TIMING 258-261
 * and 2,253-2,259 cycles at every GP0(03h) value (298 / 2,290 the first, cold): GP0(03h) changes nothing here.
 * THE RULE: run here first: `node hwtests/run.js gpu-version`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void gp0(uint32_t w) { write32(GP0, w); }
static void gp1(uint32_t w) { write32(GP1, w); }
static uint32_t gstat() { return read32(GP1); }
static void waitIdle() { for (int i = 0; i < 4000000 && (gstat() & 0x14000000u) != 0x14000000u; i++) {} }   /* 26 and 28 */

static void vramRead(int x, int y, int w, int h, uint32_t* out) {
    waitIdle();
    gp0(0xC0000000u); gp0(((uint32_t)y << 16) | (uint32_t)x); gp0(((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 4000000 && !(gstat() & (1u << 27)); i++) {}
    for (int i = 0; i < (w * h + 1) / 2; i++) out[i] = read32(GP0);
}
static void dmaUpload(const uint32_t* src, int words, int bs) {
    gp1(0x04000002u);                                           /* DMA direction CPU -> GP0 */
    write32(D2_MADR, (uint32_t)src & 0x1FFFFF);
    write32(D2_BCR, ((uint32_t)(words / bs) << 16) | (uint32_t)bs);
    write32(D2_CHCR, 0x01000201u);                              /* to the GPU, SyncMode 1, start */
    for (int i = 0; i < 4000000 && (read32(D2_CHCR) & 0x01000000u); i++) {}
    gp1(0x04000000u);
}
static void vramWrite(int x, int y, int w, int h, const uint32_t* src, int bs) {
    waitIdle();
    gp0(0xA0000000u); gp0(((uint32_t)y << 16) | (uint32_t)x); gp0(((uint32_t)h << 16) | (uint32_t)w);
    dmaUpload(src, (w * h + 1) / 2, bs);
    waitIdle();
}
static void fill(int x, int y, int w, int h, uint32_t bgr) {
    waitIdle(); gp0(0x02000000u | bgr); gp0(((uint32_t)y << 16) | (uint32_t)x); gp0(((uint32_t)h << 16) | (uint32_t)w); waitIdle();
}

#define TEXPAGE (15u | (1u << 4) | (2u << 7))                  /* X 15*64 = 960, Y 256, 15-bit */
static uint32_t saved[1024], saved2[3584], buf[256], dmaSrc[128], rowBuf[64];
static uint32_t entryStat, gpuType, infoRow[18], area3, area4, markerVal;
static uint32_t revStat, ilStat[8], tdStat[3];
static uint16_t shade[2][8][32], fillRow[96], flipRow[32], areaRow[16];
static uint16_t dmaBad[4];
static uint32_t tcyc[16][2];

/* GPUREAD = 80028001h through a VRAM read of TWO words: after a transfer GPUREAD holds its first word when it had two or
 * more, and the value from before it when it had one (amidog psxtest_gpu FIFO > READ, M223) - this test's first version
 * read one word and so left an index's answer from before in it. `markerVal` is GPUREAD read once more, after. */
static void marker() {
    uint32_t two[2];
    vramRead(1020, 511, 4, 1, two);
    markerVal = read32(GP0);
}
static uint32_t info(uint32_t idx) { waitIdle(); gp1(0x10000000u | idx); for (int i = 0; i < 64; i++) __asm__ volatile("nop"); return read32(GP0); }

int main() {
    printf("\ngpu/gpu-version\n");
    printf("The differences psx-spx lists between the old (CXD8514Q) and new (CXD8561Q) GPU that a program can see.\n");
    printf("No assertions; each row is one column's prediction.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);                  /* DMA2 on */
    entryStat = gstat();
    vramRead(896, 496, 128, 16, saved);
    vramRead(960, 384, 64, 112, saved2);                        /* the timing rectangles' column */

    gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u); gp0(0xE5000000u);
    gp0(0xE1000000u | TEXPAGE); gp0(0xE2000000u); gp0(0xE6000000u);
    for (int u = 0; u < 32; u += 2) {
        const uint32_t a = (uint32_t)u | ((uint32_t)(31 - u) << 5) | (16u << 10);
        const uint32_t b = (uint32_t)(u + 1) | ((uint32_t)(30 - u) << 5) | (16u << 10);
        rowBuf[u / 2] = a | (b << 16);
    }
    vramWrite(960, 496, 32, 1, rowBuf, 16);
    { static uint32_t w[2] = { 0x80028001u, 0x80048003u }; vramWrite(1020, 511, 4, 1, w, 1); }

    /* INFO */
    gpuType = info(7);
    gp0(0xE2000000u | 0x000F8421u);
    gp0(0xE3000000u | (2u << 10) | 1u);
    gp0(0xE4000000u | (509u << 10) | 1022u);
    gp0(0xE5000000u | ((uint32_t)(-0x55 & 0x7FF) << 11) | 0x123u);
    static const uint8_t IDX[18] = { 0x02, 0x0A, 0x12, 0x1A, 0x03, 0x0B, 0x13, 0x1B, 0x04, 0x0C, 0x05, 0x0D, 0x15, 0x1D,
                                     0x07, 0x0F, 0x17, 0x1F };
    for (int i = 0; i < 18; i++) { marker(); infoRow[i] = info(IDX[i]); }
    gp0(0xE3000000u | (0x3FFu << 10) | 0x3FFu); area3 = info(3);
    gp0(0xE4000000u | (0x3FFu << 10) | 0x3FFu); area4 = info(4);
    gp0(0xE2000000u); gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u); gp0(0xE5000000u);

    /* STAT */
    {
        const uint32_t s = gstat();
        const uint32_t mode = ((s >> 17) & 3) | (((s >> 19) & 1) << 2) | (((s >> 20) & 1) << 3) | (((s >> 21) & 1) << 4) |
                              (((s >> 22) & 1) << 5) | (((s >> 16) & 1) << 6) | (((s >> 14) & 1) << 7);
        gp1(0x08000000u | mode | 0x80u); revStat = gstat(); gp1(0x08000000u | mode);
        gp1(0x08000000u | (mode & ~0x20u));
        for (int i = 0; i < 8; i++) { ilStat[i] = gstat(); for (int k = 0; k < 2000; k++) now(); }
        gp1(0x08000000u | mode);
        gp0(0xE1000000u | TEXPAGE | (1u << 11)); tdStat[0] = gstat();             /* 1 MB mode */
        gp1(0x09000001u); gp0(0xE1000000u | TEXPAGE | (1u << 11)); tdStat[1] = gstat();   /* 2 MB mode */
        gp1(0x09000000u); tdStat[2] = gstat();                                       /* back to 1 MB */
        gp0(0xE1000000u | TEXPAGE);
    }

    /* SHADE */
    static const uint8_t C[8] = { 0x80, 0x87, 0x88, 0x8F, 0x47, 0x4F, 0x0F, 0xFF };
    for (int i = 0; i < 8; i++) {
        const uint32_t c = (uint32_t)C[i] * 0x010101u;
        const uint32_t y0 = 497u + (uint32_t)i;                 /* rows 497-504 */
        waitIdle();
        gp0(0x64000000u | c); gp0((y0 << 16) | 896u); gp0((240u << 8) | 0u); gp0((1u << 16) | 32u);
        waitIdle();
        vramRead(896, (int)y0, 32, 1, buf);
        for (int k = 0; k < 32; k++) shade[0][i][k] = (uint16_t)(buf[k >> 1] >> ((k & 1) * 16));
        const uint32_t yq = y0;                                 /* the quad over the same row, after it is read */
        waitIdle();
        gp0(0x2C000000u | c);
        gp0((yq << 16) | 896u);        gp0((240u << 8) | 0u);
        gp0((yq << 16) | 928u);        gp0((TEXPAGE << 16) | (240u << 8) | 32u);
        gp0(((yq + 1) << 16) | 896u);  gp0((241u << 8) | 0u);
        gp0(((yq + 1) << 16) | 928u);  gp0((241u << 8) | 32u);
        waitIdle();
        vramRead(896, (int)yq, 32, 1, buf);
        for (int k = 0; k < 32; k++) shade[1][i][k] = (uint16_t)(buf[k >> 1] >> ((k & 1) * 16));
    }

    /* FILL */
    fill(896, 508, 96, 1, 0);
    fill(896 + 0x0F, 508, 32, 1, 0x0000FFu);
    vramRead(896, 508, 96, 1, buf);
    for (int k = 0; k < 96; k++) fillRow[k] = (uint16_t)(buf[k >> 1] >> ((k & 1) * 16));

    /* FLIP */
    fill(896, 510, 32, 1, 0);
    gp0(0xE1000000u | TEXPAGE | (1u << 12));
    waitIdle();
    gp0(0x65000000u); gp0((510u << 16) | 896u); gp0((240u << 8) | 31u); gp0((1u << 16) | 32u);
    waitIdle();
    gp0(0xE1000000u | TEXPAGE);
    vramRead(896, 510, 32, 1, buf);
    for (int k = 0; k < 32; k++) flipRow[k] = (uint16_t)(buf[k >> 1] >> ((k & 1) * 16));

    /* AREA */
    fill(896, 496, 16, 1, 0);
    gp0(0xE3000000u | (1008u << 10) | 896u); gp0(0xE4000000u | (1023u << 10) | 1023u);
    waitIdle();
    gp0(0x60000000u | 0x00FF00u); gp0((1008u << 16) | 896u); gp0((1u << 16) | 16u);
    waitIdle();
    gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u);
    vramRead(896, 496, 16, 1, buf);
    for (int k = 0; k < 16; k++) areaRow[k] = (uint16_t)(buf[k >> 1] >> ((k & 1) * 16));

    /* DMA */
    static const int BS[4] = { 8, 16, 32, 64 };
    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < 128; i++) dmaSrc[i] = (uint32_t)(0x1000 + b * 0x100 + 2 * i) | ((uint32_t)(0x1001 + b * 0x100 + 2 * i) << 16);
        vramWrite(896, 500, 64, 4, dmaSrc, BS[b]);
        vramRead(896, 500, 64, 4, buf);
        int bad = 0;
        for (int i = 0; i < 128; i++) { const uint32_t x = buf[i] ^ dmaSrc[i]; if (x & 0xFFFFu) bad++; if (x >> 16) bad++; }
        dmaBad[b] = (uint16_t)bad;
    }

    /* TIMING */
    for (int v = 0; v < 16; v++)
        for (int r = 0; r < 2; r++) {
            waitIdle();
            gp0(0x03000000u | (uint32_t)v);
            waitIdle();
            const uint32_t w = r ? 64u : 3u, h = r ? 64u : 128u, x = r ? 960u : 1021u, y = r ? 448u : 384u;
            gp0(0x60000000u | 0x404040u); gp0((y << 16) | x);
            const uint32_t t0 = now();
            gp0((h << 16) | w);
            while ((gstat() & 0x14000000u) != 0x14000000u && now() - t0 < 2000000u) {}   /* 26 and 28: bit 28 stays clear for a draw (gpu/gpustat) */
            tcyc[v][r] = now() - t0;
        }
    waitIdle();
    gp0(0x03000000u);

    vramWrite(896, 496, 128, 16, saved, 16);
    vramWrite(960, 384, 64, 112, saved2, 16);
    gp0(0xE1000000u | (entryStat & 0x7FFu)); gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u);

    printf("\nINFO GPUSTAT at entry %08x, GP1(10h:07h) %08x\n", (unsigned)entryStat, (unsigned)gpuType);
    printf("  GPUREAD set by a VRAM read before each (it read %08x); E2 000F8421, E3 1,2, E4 1022,509, E5 +123,-55:\n",
           (unsigned)markerVal);
    for (int i = 0; i < 18; i++) printf("  %02x: %08x\n", IDX[i], (unsigned)infoRow[i]);
    printf("  E3/E4 = 3FF,3FF read through 03h/04h: %08x %08x\n", (unsigned)area3, (unsigned)area4);
    printf("STAT GP1(08h).7 set: GPUSTAT %08x (bit 14 %d)\n", (unsigned)revStat, (int)((revStat >> 14) & 1));
    printf("  interlace off, 8 reads: bit 13");
    for (int i = 0; i < 8; i++) printf(" %d", (int)((ilStat[i] >> 13) & 1));
    printf("\n  E1h.11 set: GPUSTAT %08x (bit 15 %d); GP1(09h)=1, E1h.11 again: %08x (bit 15 %d); GP1(09h)=0: %08x (bit 15 %d)\n",
           (unsigned)tdStat[0], (int)((tdStat[0] >> 15) & 1), (unsigned)tdStat[1], (int)((tdStat[1] >> 15) & 1),
           (unsigned)tdStat[2], (int)((tdStat[2] >> 15) & 1));
    for (int p = 0; p < 2; p++) {
        printf("SHADE %s, texel red = u, green = 31-u, blue 16; per c, red green blue of u = 0,8,16,24,30,31:\n",
               p ? "quad GP0(2Ch)" : "rectangle GP0(64h)");
        for (int i = 0; i < 8; i++) {
            printf("  c %02x:", C[i]);
            static const int U[6] = { 0, 8, 16, 24, 30, 31 };
            for (int j = 0; j < 6; j++) {
                const uint16_t px = shade[p][i][U[j]];
                printf(" %2d.%2d.%2d", px & 31, (px >> 5) & 31, (px >> 10) & 31);
            }
            printf("\n");
        }
        printf("  c 87, red of u = 0..31:");
        for (int k = 0; k < 32; k++) printf(" %d", shade[p][1][k] & 31);
        printf("\n");
    }
    printf("FILL at X 38Fh (896+0Fh), 32 wide: x = 896..991 filled (1) or not (0):\n  ");
    for (int k = 0; k < 96; k++) printf("%d%s", fillRow[k] ? 1 : 0, (k & 15) == 15 ? " " : "");
    printf("\nFLIP raw rectangle from u 31, X flip: red of each pixel:");
    for (int k = 0; k < 32; k++) printf(" %d", flipRow[k] & 31);
    printf("\nAREA rectangle at Y 1008 in an area at Y 1008-1023: row 496:");
    for (int k = 0; k < 16; k++) printf(" %04x", areaRow[k]);
    printf("\nDMA 64x4 upload, halfwords wrong for block size 8 16 32 64: %d %d %d %d\n", dmaBad[0], dmaBad[1], dmaBad[2], dmaBad[3]);
    printf("TIMING GP0(03h) value: cycles for a 3x128 and a 64x64 flat rectangle:\n");
    for (int v = 0; v < 16; v++) printf("  %2d: %6d %6d\n", v, (int)tcyc[v][0], (int)tcyc[v][1]);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-version: Done.\n");
    for (;;) VSync(0);
}
