/*
 * mdec/mdec-in-hang - why dma-rates' MDEC IN transfers never finish on a console, when mdec-reset's same steps do
 * (2026-10-08, for console day part 2).
 *
 * WHY. On all three consoles of console day (SCPH-5501, 7501, 9001, two runs each) every one of dma-rates' MDEC IN
 * cases printed "GAVE UP: still busy (channel stopped)": reset the MDEC, its request enables, the table command
 * 40000001h, then DMA0 with the 32 table words in SyncMode 1 - polled 32,767 turns. mdec-reset, built to ask whether
 * the reset loses a control write, makes exactly those steps and every case is done after 75-85 cycles on the same
 * consoles (STATUS "Still open in the MDEC's console tests"). This core finishes both (the DMA0 start M39's 230 cycles
 * until v0.377.30, at once since). So something else in dma-rates' setup stops the channel on a console. The
 * differences:
 *   - dma-rates never calls initVideo (psn00bsdk's ResetGraph: GPU reset, interrupt hooks), mdec-reset does;
 *   - dma-rates enables DMA0, 2, 4 and 6 in DPCR (| 08080808h), mdec-reset DMA0 alone (| 8);
 *   - dma-rates runs its OTC transfers (DMA6, SyncMode 0, CHCR 11000002h) just before;
 *   - dma-rates writes CHCR0 = 0 twice first (its CONTROL rows), mdec-reset once;
 *   - dma-rates polls CHCR from an asm loop that sums Timer 2 (startPoll), mdec-reset from C.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Each CASE is dma-rates' MDEC IN step - reset, enables,
 * command 40000001h, MADR, BCR 1 x 32, CHCR 01000201h - then polls CHCR for up to 4 ms and prints done (and when) or
 * HUNG, with the MDEC's status, DMA0's CHCR, DICR and DPCR as they stood at the end. Then it stops the channel and
 * resets the MDEC. The cases change one thing at a time, from dma-rates' own conditions:
 *   A  as dma-rates: DPCR | 08080808h, 1,024 OTC words on DMA6 just before (its last OTC case), CHCR0 = 0 twice
 *   B  A without the OTC transfer
 *   C  A with DPCR | 8 (DMA0 alone; no OTC: DMA6 is off)
 *   D  A with the OTC transfer, then DPCR | 8 again before the MDEC
 *   E  A with the poll as dma-rates' asm (its startPoll, K = 0)
 *   F  A after initVideo (ResetGraph) - last, since it cannot be undone
 * each twice. A case done here and HUNG on a console names what stops the channel.
 *
 * READ IT LIKE THIS. Done in ~70-300 cycles is the transfer; HUNG with status bit 28 (data-in request) set means the
 * MDEC asked and the DMA did not answer - the DMA side; HUNG with bit 28 clear, the MDEC never asked.
 * ON THIS CORE AT v0.377.33 (`node hwtests/run.js mdec-in-hang`): every case done.
 * `node hwtests/run.js mdec-in-hang`
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define DICR     0x1F8010F4u
#define MADR(c)  (0x1F801080u + 0x10u * (c))
#define BCR(c)   (0x1F801084u + 0x10u * (c))
#define CHCR(c)  (0x1F801088u + 0x10u * (c))
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

uint32_t mdecBuf[32] __attribute__((aligned(16)));
uint32_t otcBuf[1024] __attribute__((aligned(16)));

/* dma-rates' poll: write CHCR, read it at once, then poll it until Busy clears (asm, K = 0, at most 32,767 turns). */
static uint32_t asmPoll(uint32_t chcr, uint32_t val) {
    uint32_t iters;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t"
        "sw %[val], 0(%[chcr])\n\t"
        "li %[iters], 0\n"
        "1:\n\t"
        "lw $t3, 0(%[chcr])\n\t"
        "nop\n\t"
        "srl $t3, $t3, 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "beqz $t3, 2f\n\t"
        "nop\n\t"
        "addiu %[iters], %[iters], 1\n\t"
        "sltiu $t4, %[iters], 0x7fff\n\t"
        "bnez $t4, 1b\n\t"
        "nop\n"
        "2:\n\t"
        ".set pop\n"
        : [iters] "=&r"(iters)
        : [chcr] "r"(chcr), [val] "r"(val)
        : "t3", "t4", "memory");
    return iters;
}

struct Row { bool done; uint32_t at, status, chcr, dicr, dpcr, iters; };

static void otc() {
    write32(MADR(6), (uint32_t)&otcBuf[1023] & 0x1FFFFFu);
    write32(BCR(6), 1024);
    write32(CHCR(6), 0x11000002u);
    for (int i = 0; i < 100000 && (read32(CHCR(6)) & 0x01000000u); i++) { }
}

static Row mdecCase(bool asmLoop) {
    write32(MDEC1, 0x80000000u);                                /* reset */
    write32(MDEC1, 0x60000000u);                                /* data-in and data-out requests on */
    write32(MDEC0, 0x40000001u);                                /* the quantisation tables, 32 words */
    write32(MADR(0), (uint32_t)mdecBuf & 0x1FFFFFu);
    write32(BCR(0), (1u << 16) | 32u);
    Row r;
    r.iters = 0;
    const uint32_t t0 = now();
    if (asmLoop) {
        r.iters = asmPoll(CHCR(0), 0x01000201u);
        r.done = r.iters < 0x7FFF;
        r.at = now() - t0;
    } else {
        write32(CHCR(0), 0x01000201u);
        r.done = false; r.at = 0;
        for (;;) {
            const uint32_t t = now();
            if (!(read32(CHCR(0)) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
            if (t - t0 > 135475u) break;                        /* 4 ms */
        }
    }
    r.status = read32(MDEC1); r.chcr = read32(CHCR(0)); r.dicr = read32(DICR); r.dpcr = read32(DPCR);
    write32(CHCR(0), 0);                                        /* stop the channel */
    write32(MDEC1, 0x80000000u);
    return r;
}

static void show(const char* name, const Row& r) {
    if (r.done) printf("  %-44s done after %6u cycles", name, (unsigned)r.at);
    else printf("  %-44s HUNG (%u cycles)          ", name, (unsigned)r.at);
    printf("   MDEC %08x (in-req %u)  CHCR0 %08x  DICR %08x  DPCR %08x\n", (unsigned)r.status,
           (unsigned)((r.status >> 28) & 1), (unsigned)r.chcr, (unsigned)r.dicr, (unsigned)r.dpcr);
}

static void caseA(const char* name, uint32_t dpcrOr, bool withOtc, bool dpcrAgain, bool asmLoop) {
    for (int w = 0; w < 2; w++) {
        write32(DPCR, read32(DPCR) | dpcrOr);
        if (withOtc) otc();
        if (dpcrAgain) write32(DPCR, read32(DPCR) | 0x8u);
        write32(CHCR(0), 0); write32(CHCR(0), 0);               /* dma-rates' CONTROL rows */
        show(name, mdecCase(asmLoop));
    }
}

int main() {
    printf("\nmdec/mdec-in-hang\n");
    printf("dma-rates' MDEC IN step under each of its conditions, one changed at a time. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 32; i++) mdecBuf[i] = 0x01010101u * (uint32_t)(i + 1);
    const uint32_t dpcr0 = read32(DPCR);
    printf("DPCR at the start %08x, DICR %08x, MDEC status %08x\n", (unsigned)dpcr0, (unsigned)read32(DICR), (unsigned)read32(MDEC1));
    caseA("A  as dma-rates (DPCR|08080808, OTC first)", 0x08080808u, true, false, false);
    write32(DPCR, dpcr0);
    caseA("B  A without the OTC transfer", 0x08080808u, false, false, false);
    write32(DPCR, dpcr0);
    caseA("C  DPCR|8 alone, no OTC", 0x8u, false, false, false);
    write32(DPCR, dpcr0);
    caseA("D  A, then DPCR|8 again after the OTC", 0x08080808u, true, true, false);
    write32(DPCR, dpcr0);
    caseA("E  A with dma-rates' asm poll", 0x08080808u, true, false, true);
    write32(DPCR, dpcr0);
    ExitCriticalSection();
    initVideo(320, 240);
    EnterCriticalSection();
    lastT = read16(T2_COUNT);
    printf("after initVideo: DPCR %08x, DICR %08x\n", (unsigned)read32(DPCR), (unsigned)read32(DICR));
    caseA("F  A after initVideo (ResetGraph)", 0x08080808u, true, false, false);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nmdec/mdec-in-hang: Done.\n");
    for (;;) VSync(0);
}
