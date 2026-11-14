/*
 * cpu-feed-cost2: THE SAME LOOPS WHILE THE GPU DRAWS - does a GPUSTAT read, a RAM access or a timer read cost more then?
 *
 * WHY. hwtests/cpu-feed-cost on the second SCPH-5501 and the PSone: with the GPU idle every loop - RAM copy / store / load,
 * GPUSTAT read, GP0 store, Timer 2 read - reads v0.377.348 to the cycle. But in hwtests/gpu-doom-feed a call of the feed loop
 * that finds the GPU busy (it sends nothing: eight words copied, one GPUSTAT read) takes 201.5 cycles on those consoles
 * and 194.0 here, and one that finds it ready 333.5 against 334.4. So the 7.5 cycles are spent beside a GPU that is
 * drawing, on one of those instructions.
 *
 * WHAT. cpu-feed-cost's loops (64 turns, cached code, Timer 2 around them) started right behind a long draw's last word,
 * so the whole loop runs while the GPU draws: a 256x128 textured sprite (the game's sky: ~60,000 cycles), a 240x200
 * textured triangle, a 240x200 flat triangle, a 512x256 fill - and idle, the control. Loops: empty, ram copy, ram load,
 * stat read, t2 read. Five times each (the first fetches the code); GPUSTAT bit 26 read after each loop to show the draw
 * was still going (b 0) - a row with b 1 ended inside the loop and is not what is asked. At 256x240, in whatever part of
 * the frame the loop falls. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js cpu-feed-cost2 --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define N 64

static uint32_t src[N] __attribute__((aligned(16)));
static uint32_t dst[N] __attribute__((aligned(16)));
static volatile uint32_t sink;

#define LOOP(BODY) __asm__ volatile(".set push\n.set noreorder\n" \
    "li $t3, 64\n" \
    "1:\n" BODY \
    "addiu $t3, $t3, -1\n" \
    "addiu %[s], %[s], 4\n" \
    "bnez $t3, 1b\n" \
    "addiu %[d], %[d], 4\n" \
    ".set pop\n" : [s] "+r"(s), [d] "+r"(d), [x] "+r"(x) : [io] "r"(io), [z] "r"(z) : "t3", "t4", "memory")
static uint32_t __attribute__((noinline)) run(int which) {
    uint32_t* s = src; uint32_t* d = dst; uint32_t x = 0; const uint32_t io = 0x1F800000u, z = 0;
    const uint16_t t0 = read16(T2_COUNT);
    switch (which) {
        case 0: LOOP(""); break;
        case 1: LOOP("lw $t4, 0(%[s])\nnop\nsw $t4, 0(%[d])\n"); break;
        case 2: LOOP("sw %[z], 0(%[d])\n"); break;
        case 3: LOOP("lw $t4, 0(%[s])\nnop\naddu %[x], %[x], $t4\n"); break;
        case 4: LOOP("lw $t4, 0x1814(%[io])\nnop\naddu %[x], %[x], $t4\n"); break;
        case 5: LOOP("sw %[z], 0x1810(%[io])\n"); break;
        case 6: LOOP("lw $t4, 0(%[s])\nnop\nsw $t4, 0x1810(%[io])\n"); break;
        default: LOOP("lhu $t4, 0x1120(%[io])\nnop\naddu %[x], %[x], $t4\n"); break;
    }
    const uint16_t t1 = read16(T2_COUNT);
    sink = x + (uint32_t)(s - src) + (uint32_t)(d - dst);
    return (uint16_t)(t1 - t0);
}

static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static void idle() { for (int i = 0; i < 2000000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void __attribute__((noinline)) kick(int kind) {
    switch (kind) {
        case 1: write32(GP0, 0x65808080u); write32(GP0, xy(0, 0)); write32(GP0, 0x3c000000u); write32(GP0, xy(256, 128)); break;   /* a textured sprite */
        case 2: write32(GP0, 0x24808080u); write32(GP0, xy(0, 0)); write32(GP0, 0x3c000000u); write32(GP0, xy(240, 0)); write32(GP0, 0x008c00f0u); write32(GP0, xy(0, 200)); write32(GP0, 0x0000c800u); break;
        case 3: write32(GP0, 0x20808080u); write32(GP0, xy(0, 0)); write32(GP0, xy(240, 0)); write32(GP0, xy(0, 200)); break;
        case 4: write32(GP0, 0x02101010u); write32(GP0, xy(0, 0)); write32(GP0, xy(512, 256)); break;
        default: break;
    }
}
int main() {
    printf("\ncpu/cpu-feed-cost2\n");
    printf("cpu-feed-cost's loops (64 turns, cycles by Timer 2) right behind a long draw's last word; b = GPUSTAT bit 26 after the loop\n");
    printf("(0: the GPU still drawing). Five times each. No assertions.\n\n");
    for (int i = 0; i < N; i++) { src[i] = 0; dst[i] = 0; }
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    idle();
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int q = 0; q < 4; q++) { idle(); write32(GP0, 0x02305070u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    idle();
    write32(GP1, 0x08000000u); write32(GP1, 0x03000000u);
    static const char* const KN[5] = { "idle", "sprite 256x128", "tex tri 240x200", "flat tri 240x200", "fill 512x256" };
    static const char* const NM[5] = { "empty", "ram copy", "ram load", "stat read", "t2 read" };
    static const int WH[5] = { 0, 1, 3, 4, 7 };
    static uint16_t r[5][5][5]; static uint8_t bz[5][5][5];
    for (int p = 0; p < 5; p++) for (int k = 0; k < 5; k++) for (int w = 0; w < 5; w++) {
        idle();
        kick(k);
        r[k][w][p] = (uint16_t)run(WH[w]);
        bz[k][w][p] = (uint8_t)((read32(GP1) >> 26) & 1);
    }
    idle();
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(1023, 511));
    idle();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < 5; k++) {
        printf("%s:\n", KN[k]);
        for (int w = 0; w < 5; w++) {
            printf("  %-10s", NM[w]);
            for (int p = 0; p < 5; p++) printf(" %u", (unsigned)r[k][w][p]);
            printf("   b");
            for (int p = 0; p < 5; p++) printf(" %u", (unsigned)bz[k][w][p]);
            printf("\n");
        }
    }
    printf("\ncpu/cpu-feed-cost2: Done.\n");
    for (;;) VSync(0);
}
