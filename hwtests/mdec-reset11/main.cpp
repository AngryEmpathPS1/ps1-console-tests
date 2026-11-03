/*
 * mdec/mdec-reset11 - mdec-reset10's pair (a cold call, then the same call warm) in finer bins, and what the warm call's loss
 * needs (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset10 on the SCPH-5501 (two runs, byte-identical): a cold call of mdec-reset's one() never loses its enables at
 * any refresh phase, but the warm call right behind it loses DMA0 in 16 of 31 tries when the cold call started at phase
 * 240-247 of the 256-cycle refresh, and never at another - with a normal status read (not the bus), as if the reset landed
 * after the enables and took them away. mdec-reset's own N 0 (lost at locked starts 0, 16 and 232 of 256, kept at 64, 128
 * and 192) is its first row: the program's first call, cold, and the recorded call right behind it.
 *
 * WHAT IT DOES. one() (the reset, the enables, the status read, the table command, a 32-word DMA0), interrupts off, 2,048
 * tries a variant, the phase of Timer 2 (16 bits, whole refreshes) read just before the first call, in bins of 2:
 *   P   evict, one() cold, one() warm at once (mdec-reset10's pair)
 *   W   evict, one() cold, 200 cycles, one() warm: does time between the calls take the loss away?
 *   N   evict, oneNoDma() cold (one() without its DMA0: the command written, the channel never started), one() warm: does the
 *       cold call's table DMA matter?
 *   C   one() warm, one() warm at once (no eviction; the first call's phase): the control
 * For the second call of each try: lost (DMA0 not done in 3,000 cycles) and bus (status read 60000000h).
 *
 * READ IT LIKE THIS. Per bin and variant, calls/lost/bus of the second call. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR0    0x1F801080u
#define BCR0     0x1F801084u
#define CHCR0    0x1F801088u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t table[32] __attribute__((aligned(16)));
struct Row { uint32_t status, at; bool done; };
static __attribute__((noinline, noclone)) Row one(uint32_t wait, bool cmdFirst, bool twice) {
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    if (twice) write32(MDEC1, 0x60000000u);
    if (cmdFirst) write32(MDEC0, 0x40000001u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);
    Row r;
    r.status = read32(MDEC1);
    if (!cmdFirst) write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    const uint32_t t0 = now();
    write32(CHCR0, 0x01000201u);
    r.done = false; r.at = 0;
    for (;;) {
        const uint32_t t = now();
        if (!(read32(CHCR0) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
        if (t - t0 > 3000u) break;
    }
    write32(CHCR0, 0);
    return r;
}
/* one() up to its command, the channel never started */
static __attribute__((noinline, noclone)) uint32_t oneNoDma(uint32_t wait) {
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);
    const uint32_t s = read32(MDEC1);
    write32(MDEC0, 0x40000001u);
    write32(CHCR0, 0);
    return s;
}

extern "C" void mr11Evict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mr11Evict\nmr11Evict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

static uint8_t calls[4][128], lost[4][128], bus[4][128];
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
static volatile uint32_t sink;

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset11\n");
    printf("A call of mdec-reset's one() right behind another, by the first call's refresh phase. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    for (int v = 0; v < 4; v++)
        for (int i = 0; i < 2048; i++) {
            waitCycles(300 + (uint32_t)(i * 37 % 256) + (uint32_t)((i >> 8) & 7));
            if (v != 3) mr11Evict();
            const uint32_t phase = read16(T2_COUNT) & 0xFF;
            if (v == 2) sink = oneNoDma(zeroWait); else one(zeroWait, zeroFlag, zeroFlag);
            if (v == 1) waitCycles(200);
            const Row r = one(zeroWait, zeroFlag, zeroFlag);
            const int b = (int)(phase / 2);
            if (calls[v][b] < 255) calls[v][b]++;
            if (!r.done && lost[v][b] < 255) lost[v][b]++;
            if (r.status == 0x60000000u && bus[v][b] < 255) bus[v][b]++;
        }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("phase   P calls lost bus   W calls lost bus   N calls lost bus   C calls lost bus\n");
    for (int b = 0; b < 128; b++) {
        printf("%3d ", b * 2);
        for (int v = 0; v < 4; v++) printf("    %5d %4d %3d", (int)calls[v][b], (int)lost[v][b], (int)bus[v][b]);
        printf("\n");
    }
    for (int v = 0; v < 4; v++) {
        int c = 0, l = 0, s = 0;
        for (int b = 0; b < 128; b++) { c += calls[v][b]; l += lost[v][b]; s += bus[v][b]; }
        printf("total %c: calls %d lost %d bus %d\n", "PWNC"[v], c, l, s);
    }
    printf("\nmdec/mdec-reset11: Done.\n");
    for (;;) {}   /* park after Done. */
}
