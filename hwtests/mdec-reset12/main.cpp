/*
 * mdec/mdec-reset12 - the reset W cycles after a table DMA starts, every W from 40 to 240 (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset11 on the SCPH-5501 (two runs, byte-identical): a call of mdec-reset's one() right behind another loses its
 * DMA0 in narrow windows of the first call's refresh phase (cold-then-warm 240-241: 36 of 36; warm-then-warm 124-125 and
 * 136-137: 46 of 46), never with 200 cycles between the calls and never when the first call ran no table DMA. Its status
 * read is normal. So the reset meets the MDEC still busy with the table the first call's DMA0 gave it, and the refresh only
 * moves the reset's arrival by a few cycles. This puts the reset at a known distance from the table DMA instead.
 *
 * WHAT IT DOES. Interrupts off, cached code. The MDEC settled (reset, 400 cycles); the enables; the table command (40000001h);
 * DMA0 started with its 32 words (SyncMode 1); then a cached sled of W nops (the CPU runs on while the DMA has the bus); then
 * the reset, 20 nops, the enables, the status read, the table command again and DMA0 again, polled for 3,000 cycles. W 40 to
 * 240, 4 tries each. Per W: the second DMA0's lost count, and the status read (the last try's).
 *
 * READ IT LIKE THIS. Lost at some W and not others is the reset meeting the MDEC busy with the first table; the status says
 * what it took (80040000h a reset taken; anything with bit 29 set a command still running). No assertions.
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

/* 256 nops then return: entered at word 256 - W it runs W nops (cached after the first pass). */
extern "C" void mr12Sled(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 16\n.global mr12Sled\nmr12Sled:\n.rept 256\nnop\n.endr\njr $ra\nnop\n.set pop\n");

/* The whole try in one cached function: the first table, the sled, the reset and what follows. */
static __attribute__((noinline, noclone)) uint32_t tryW(uint32_t w, uint32_t *status) {
    void (*sled)(void) = (void (*)(void))((uint32_t)mr12Sled + 4u * (256u - w));
    write32(MDEC1, 0x60000000u);
    write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    write32(CHCR0, 0x01000201u);
    sled();
    write32(MDEC1, 0x80000000u);
    __asm__ volatile(".set push\n.set noreorder\n.rept 20\nnop\n.endr\n.set pop\n");
    write32(MDEC1, 0x60000000u);
    *status = read32(MDEC1);
    { const uint32_t t1 = now(); while ((read32(CHCR0) & 0x01000000u) && now() - t1 < 3000u) {} }   /* the first DMA0, if it is still going */
    write32(CHCR0, 0);
    write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    const uint32_t t0 = now();
    write32(CHCR0, 0x01000201u);
    for (;;) {
        const uint32_t t = now();
        if (!(read32(CHCR0) & 0x01000000u)) { write32(CHCR0, 0); return 0; }
        if (t - t0 > 3000u) { write32(CHCR0, 0); return 1; }
    }
}

static uint8_t lost[201];
static uint32_t stat[201];

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset12\n");
    printf("The reset W cycles after a table DMA starts, then the enables, a table command and DMA0. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    for (int t = 0; t < 5; t++)
        for (int w = 40; w <= 240; w++) {
            write32(MDEC1, 0x80000000u); waitCycles(400);
            uint32_t s = 0;
            const uint32_t l = tryW((uint32_t)w, &s);
            if (t == 0) continue;   /* the first pass warms the cache */
            lost[w - 40] += (uint8_t)l;
            stat[w - 40] = s;
        }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  W   lost of 4   status\n");
    for (int w = 40; w <= 240; w++) printf("%4d   %d   %08x\n", w, (int)lost[w - 40], (unsigned)stat[w - 40]);
    printf("\nmdec/mdec-reset12: Done.\n");
    for (;;) {}   /* park after Done. */
}
