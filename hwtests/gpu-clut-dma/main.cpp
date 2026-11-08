/*
 * gpu/gpu-clut-dma - a CLUT reload in a GPU linked list: does the walk wait for it?
 *
 * WHY (2026-11-07). hwtests/gpu-clut-ready / -ready2 on the second SCPH-5501: a sprite's CLUT reload runs from the word that
 * names the CLUT and holds the command's words in the FIFO - GPUSTAT.28 clear - until it has run (an 8-bit reload ~280
 * cycles). Games feed sprites by DMA linked lists, whose request in direction 2 is (psx-spx) bit 28; gpu-dreq-fill found it
 * the FIFO's emptiness. Does a list's walk stop at the node after a reloading sprite until the reload is done - or do the
 * next nodes' words go in behind it?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, GP1(04h)=2, the vertical display range empty, the kick ~300
 * cycles after Timer 1's tick. A list: a textured 8x8 GP0(64h) sprite, then a tail of 8 header-only nodes (H), 8 nodes of one
 * GP0(00h) word (N1) or of four (N4), or a second sprite with the same CLUT (S). Before each kick the sprite is drawn by the
 * CPU with the OTHER CLUT (rows "r": the list's sprite reloads) or the same one ("c"). After CHCR2's store, 200 reads back to
 * back of GPUSTAT, CHCR2 and MADR2 (150 reads, ~15 cycles a read), each with Timer 2. Two passes, the second kept.
 *
 * READ IT LIKE THIS. A row: depth, CLUT, tail; then t:b:n at the first read and at every change - t Timer 2 from the read
 * before the kick, b bits 28 26 25 as 4/2/1 plus 8 while CHCR2 is busy, n the node MADR2 points at (0 the sprite's, 1..8 the
 * tail's, 9 the end marker, ? elsewhere). If the walk waits for the reload, a reloading row's n stays at 1 for ~the reload.
 *
 * ON THIS CORE (v0.377.299, `node hwtests/run.js gpu-clut-dma --frames=3000 --quiet=1500`) and the knob core
 * (experiments/_mkclut28.js): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define READS 150
/* the scratchpad's 1 KB: halfwords [i] Timer 2 and [150 + i] MADR2's low half; bytes 600 + i GPUSTAT >> 25 and 750 + i CHCR2 >>
 * 24; Timer 2 before the kick at byte 1008 */
#define SPAD ((volatile uint16_t *)0x1F800000u)
#define SPADB ((volatile uint8_t *)0x1F800000u)

static uint32_t list[64] __attribute__((aligned(16)));
static uint32_t nodeAt[10];
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void delay(uint32_t n) { __asm__ volatile(".set noreorder\n" "1: bnez %0, 1b\n" "addiu %0, %0, -1\n" ".set reorder\n" : "+r"(n)); }
static void lockLine() { const uint32_t t = read16(T1_COUNT); for (int i = 0; i < 100000 && read16(T1_COUNT) == t; i++) {} }
static uint32_t addr24(const uint32_t* p) { return (uint32_t)p & 0xFFFFFFu; }

static const uint32_t CLUT_A = 480u << 6, CLUT_B = 481u << 6;
static void spriteWords(uint32_t* w, uint32_t clut) { w[0] = 0x64808080u; w[1] = (300u << 16) | 0u; w[2] = clut << 16; w[3] = (8u << 16) | 8u; }

/* tail: 0 H, 1 N1, 4 N4, 9 S */
static void build(int tail) {
    uint32_t* p = list;
    nodeAt[0] = addr24(p);
    spriteWords(p + 1, CLUT_A);
    uint32_t* q = p + 5;
    p[0] = (4u << 24) | addr24(q);
    p = q;
    if (tail == 9) {
        nodeAt[1] = addr24(p);
        spriteWords(p + 1, CLUT_A);
        q = p + 5; p[0] = (4u << 24) | addr24(q); p = q;
        for (int i = 2; i <= 8; i++) nodeAt[i] = 0xFFFFFFFFu;
    } else {
        for (int i = 1; i <= 8; i++) {
            nodeAt[i] = addr24(p);
            for (int w = 0; w < tail; w++) p[1 + w] = 0x00000000u;
            q = p + 1 + tail; p[0] = ((uint32_t)tail << 24) | addr24(q); p = q;
        }
    }
    nodeAt[9] = addr24(p);
    p[0] = 0x00FFFFFFu;
}
static int nodeOf(uint32_t madr) {
    for (int i = 0; i <= 9; i++) if (nodeAt[i] == (madr & 0xFFFFFFu)) return i;
    return -1;
}
/* the kick, then READS reads of GPUSTAT, CHCR2, MADR2 and Timer 2 into the scratchpad, back to back */
static __attribute__((noinline, noclone)) void sample(uint32_t chcr) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "li    $t8, 150\n"
        "move  $t6, $t0\n"
        "move  $t9, $t0\n"
        "lhu   $t5, 0x1120($t0)\n"
        "nop\n"
        "sh    $t5, 0x3f0($t0)\n"      /* Timer 2 before the kick */
        "sw    %0, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t7, 0x1814($t0)\n"      /* GPUSTAT */
        "lw    $t4, 0x10a8($t0)\n"      /* CHCR2 */
        "lw    $t3, 0x10a0($t0)\n"      /* MADR2 */
        "lhu   $t5, 0x1120($t0)\n"      /* Timer 2 */
        "srl   $t7, $t7, 25\n"
        "srl   $t4, $t4, 24\n"
        "sh    $t5, 0($t6)\n"
        "sh    $t3, 300($t6)\n"
        "sb    $t7, 600($t9)\n"
        "sb    $t4, 750($t9)\n"
        "addiu $t9, $t9, 1\n"
        "addiu $t8, $t8, -1\n"
        "bnez  $t8, 1b\n"
        "addiu $t6, $t6, 2\n"
        ".set pop\n"
        : : "r"(chcr) : "t0", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "memory");
}
static void upload(int x, int y, int wHalf, int h, uint16_t (*val)(int i)) {
    gpuIdle();
    write32(GP0, 0xA0000000u); write32(GP0, ((uint32_t)y << 16) | (uint32_t)x); write32(GP0, ((uint32_t)h << 16) | (uint32_t)wHalf);
    for (int i = 0; i < wHalf * h; i += 2) write32(GP0, (uint32_t)val(i) | ((uint32_t)val(i + 1) << 16));
    gpuIdle();
}
static uint16_t texv(int) { return 0x5A3C; }
static uint16_t clutAv(int i) { return (uint16_t)(0x0400 | (i & 0x1F)); }
static uint16_t clutBv(int i) { return (uint16_t)(((i & 0x1F) << 5) | 1); }

static const int TAILS[4] = { 0, 1, 4, 9 };
static const char* TN[4] = { "H", "N1", "N4", "S" };
static uint16_t tt[16][READS], bb[16][READS];
static int8_t nn[16][READS];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-clut-dma\n");
    printf("A CLUT reload in a GPU linked list: GPUSTAT, CHCR2 and MADR2 read back to back from the kick. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0100);                              /* Timer 1 counts hblanks */
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    upload(512, 0, 16, 8, texv);
    upload(0, 480, 256, 1, clutAv);
    upload(0, 481, 256, 1, clutBv);
    write32(GP1, 0x07000000u | (16u << 10) | 16u);         /* vertical display range empty */
    for (int pass = 0; pass < 2; pass++) for (int r = 0; r < 16; r++) {
        const bool d8 = (r & 8) != 0, reload = (r & 4) == 0; const int tail = TAILS[r & 3];
        write32(GP0, 0xE1000000u | (d8 ? 0x88u : 0x08u));
        build(tail);
        uint32_t pw[4]; spriteWords(pw, reload ? CLUT_B : CLUT_A);
        gpuIdle();
        for (int i = 0; i < 4; i++) write32(GP0, pw[i]);   /* the prime: the other CLUT, or the same */
        gpuIdle();
        write32(GP1, 0x04000002u);
        write32(MADR2, addr24(list));
        write32(BCR2, 0);
        lockLine(); delay(150);
        sample(0x01000401u);
        for (int i = 0; i < 2000000 && (read32(CHCR2) & 0x01000000u); i++) {}
        gpuIdle();
        write32(GP1, 0x04000000u);
        if (pass == 1) for (int i = 0; i < READS; i++) {
            tt[r][i] = (uint16_t)(SPAD[i] - SPAD[0x3f0 / 2]);
            const uint8_t g = SPADB[600 + i], c = SPADB[750 + i];
            bb[r][i] = (uint16_t)(((g >> 3) & 1) << 2 | ((g >> 1) & 1) << 1 | (g & 1) | ((c & 1) ? 8 : 0));
            nn[r][i] = (int8_t)nodeOf((uint32_t)SPAD[150 + i] | ((uint32_t)addr24(list) & 0xFF0000u));
        }
    }
    gpuIdle();
    initVideo(320, 240);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int r = 0; r < 16; r++) {
        printf("%s %s %-2s:", (r & 8) ? "8-bit" : "4-bit", (r & 4) ? "c" : "r", TN[r & 3]);
        int lb = -1, ln = -2;
        for (int i = 0; i < READS; i++) if (bb[r][i] != lb || nn[r][i] != ln) {
            lb = bb[r][i]; ln = nn[r][i];
            if (ln >= 0) printf(" %u:%u:%d", (unsigned)tt[r][i], (unsigned)lb, ln); else printf(" %u:%u:?", (unsigned)tt[r][i], (unsigned)lb);
        }
        printf("\n");
    }
    printf("\ngpu/gpu-clut-dma: Done.\n");
    for (;;) VSync(0);
}
