/*
 * mdec/mdec-tablebusy - how long the MDEC stays busy (status bit 29) after a quant or scale table upload.
 *
 * WHY (2026-11-14; the console fuzzer's 4001-6 and 902-4, STATUS "mdec-tablebusy"). After command 3 (the scale table) and its 32
 * words written by the CPU, a status read gives A004FFFFh on the second SCPH-5501 and the SCPH-7502 - still busy - and 8004FFFFh
 * on this core (v0.377.348), which ends a table the moment its last word lands (src/psx.js pushParam). Nothing had timed it.
 *
 * WHAT IT DOES (Timer 2 at the system clock). Three uploads: quant mono (command 2, 16 words), quant colour (command 2 bit 0, 32
 * words), scale (command 3, 32 words), the words common/'s tables.
 *   P  the CPU writes the command and its words; then a poll of the status and the timer until bit 29 clears: cycles from the
 *      last word's store to the first idle read, the status at the first read and after.
 *   W  the status after every word (bit 29 and the remaining-word field), and once more 2,000 cycles after the last.
 *   S  W cycles between words (0, 100, 400, 1,600): the time from the last word to idle - a drain behind the words or work
 *      after the last one.
 *   N  one word short: the status 5,000 cycles on (does a short table end on its own?).
 *   Z  the scale table all zeros against the real one (does the content matter?).
 *   D  the table by DMA0, bs 16 and 32: CHCR to DMA0 done, and to idle.
 *   F  the fine scan: the same upload, then n nops (a sled in cached code, run warm) and one status read, n = 0 .. 600: the n
 *      where bit 29 first reads clear, to the cycle; a scan that never sees it clear says so.
 * Every wait is bounded; every row resets the MDEC first (and waits out the reset's deaf cycles, mdec-reset2).
 *
 * READ IT LIKE THIS. Cycles. P / S / D times are the poll's (~10-20 a read); F is to the cycle, against this core's own scan.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>
#include <mdec.h>

#define MDEC_STAT 0x1F801824
#define DMA0_MADR 0x1F801080
#define DMA0_BCR  0x1F801084
#define DMA0_CHCR 0x1F801088
#define DPCR      0x1F8010F0
#define BUSY      (1u << 29)
#define SLED      600

extern "C" uint32_t stat_after(uint32_t n);
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".balign 16\n.global stat_after\n"
    "stat_after:\n"
    "  la    $t0, sled_end\n"
    "  sll   $t1, $a0, 2\n"
    "  subu  $t0, $t0, $t1\n"
    "  jr    $t0\n"
    "  nop\n"
    "  .rept 600\n  nop\n  .endr\n"
    "sled_end:\n"
    "  lui   $t2, 0x1f80\n"
    "  lw    $v0, 0x1824($t2)\n"
    "  jr    $ra\n"
    "  nop\n"
    ".set pop\n");

static uint32_t words[3][32];
static uint32_t zeros[32];
static const int NW[3] = { 16, 32, 32 };
static const uint32_t CMD[3] = { 2u << 29, (2u << 29) | 1, 3u << 29 };
static const char* const NAME[3] = { "quant mono ", "quant color", "scale      " };
static uint32_t dmabuf[33] __attribute__((aligned(4)));

static uint32_t clk; static uint16_t last;
static inline uint32_t now() { const uint16_t t = readTimer(2); clk += (uint16_t)(t - last); last = t; return clk; }
static inline void zero() { clk = 0; last = readTimer(2); }
static void waitc(uint32_t g) { const uint32_t t0 = now(); while (now() - t0 < g) {} }
static inline uint32_t st() { return read32(MDEC_STAT); }

static void reset() { mdec_ctrl(1u << 31); st(); st(); st(); st(); mdec_ctrl(0); waitc(200); }

/* the command and n words of src, spaced by gap cycles; returns the clock at the last store */
static uint32_t upload(int t, const uint32_t* src, int n, uint32_t gap) {
    write32(MDEC_CMD, CMD[t]);
    for (int i = 0; i < n; i++) { if (gap) waitc(gap); write32(MDEC_DATA, src[i]); }
    return now();
}
/* poll until idle: cycles from `from` to the first idle read; first = the first status read */
static uint32_t untilIdle(uint32_t from, uint32_t* first, uint32_t* after, uint32_t limit) {
    uint32_t s = st(), t = now();
    *first = s;
    while (s & BUSY) { if (t - from > limit) { *after = s; return 0xFFFFFFFFu; } s = st(); t = now(); }
    *after = s;
    return t - from;
}

int main() {
    printf("\nmdec/mdec-tablebusy\n");
    printf("How long the MDEC stays busy after a table upload (Timer 2, system clock). No assertions - compare against your emulator.\n\n");
    for (int i = 0; i < 16; i++) words[0][i] = quant[i * 4] | (quant[i * 4 + 1] << 8) | (quant[i * 4 + 2] << 16) | (quant[i * 4 + 3] << 24);
    for (int i = 0; i < 32; i++) words[1][i] = quant[i * 4] | (quant[i * 4 + 1] << 8) | (quant[i * 4 + 2] << 16) | (quant[i * 4 + 3] << 24);
    for (int i = 0; i < 32; i++) words[2][i] = (uint16_t)idct[i * 2] | ((uint32_t)(uint16_t)idct[i * 2 + 1] << 16);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    uint32_t f, a;

    printf("P: the CPU uploads, then a poll until bit 29 clears (cycles from the last store; 3 runs)\n");
    for (int t = 0; t < 3; t++) {
        printf("  %s", NAME[t]);
        for (int r = 0; r < 3; r++) {
            reset(); zero();
            const uint32_t t1 = upload(t, words[t], NW[t], 0);
            const uint32_t d = untilIdle(t1, &f, &a, 100000);
            printf("  writes %4d idle +%5d (first %08x then %08x)", (int)t1, (int)d, (int)f, (int)a);
        }
        printf("\n");
    }

    printf("\nW: the status after each word (busy bit, remaining field), then 2,000 cycles on\n");
    for (int t = 0; t < 3; t++) {
        static uint32_t s[33];
        reset(); zero();
        write32(MDEC_CMD, CMD[t]);
        s[0] = st();
        for (int i = 0; i < NW[t]; i++) { write32(MDEC_DATA, words[t][i]); s[i + 1] = st(); }
        waitc(2000);
        const uint32_t s2 = st();
        printf("  %s:", NAME[t]);
        for (int i = 0; i <= NW[t]; i++) printf(" %d.%04x", (int)((s[i] >> 29) & 1), (int)(s[i] & 0xFFFF));
        printf(" | +2000 %08x\n", (int)s2);
    }

    printf("\nS: W cycles between words, then a poll (cycles from the last store)\n");
    static const uint32_t GAPS[] = { 0, 100, 400, 1600 };
    for (int t = 0; t < 3; t++) {
        printf("  %s", NAME[t]);
        for (int g = 0; g < 4; g++) {
            reset(); zero();
            const uint32_t t1 = upload(t, words[t], NW[t], GAPS[g]);
            const uint32_t d = untilIdle(t1, &f, &a, 100000);
            printf("  W %4d: +%5d", (int)GAPS[g], (int)d);
        }
        printf("\n");
    }

    printf("\nN: one word short, the status 5,000 cycles on, then the last word and a poll\n");
    for (int t = 0; t < 3; t++) {
        reset(); zero();
        upload(t, words[t], NW[t] - 1, 0);
        waitc(5000);
        const uint32_t s1 = st();
        write32(MDEC_DATA, words[t][NW[t] - 1]);
        const uint32_t t1 = now();
        const uint32_t d = untilIdle(t1, &f, &a, 100000);
        printf("  %s: short %08x; the last word: idle +%d (first %08x then %08x)\n", NAME[t], (int)s1, (int)d, (int)f, (int)a);
    }

    printf("\nZ: the scale table all zeros, then the real one\n ");
    for (int k = 0; k < 2; k++) {
        reset(); zero();
        const uint32_t t1 = upload(2, k ? words[2] : zeros, 32, 0);
        const uint32_t d = untilIdle(t1, &f, &a, 100000);
        printf(" %s: idle +%d", k ? "real " : "zeros", (int)d);
    }
    printf("\n");
    reset(); upload(2, words[2], 32, 0); waitc(3000);   /* the real table back in */

    printf("\nD: by DMA0 (the command by the CPU, then the words): CHCR to DMA0 done, and to idle\n");
    static const int BS[] = { 16, 32 };
    for (int t = 0; t < 3; t++) {
        printf("  %s", NAME[t]);
        for (int b = 0; b < 2; b++) {
            const int bs = BS[b] > NW[t] ? NW[t] : BS[b];
            for (int i = 0; i < NW[t]; i++) dmabuf[i] = words[t][i];
            reset();
            mdec_ctrl(1u << 30);
            write32(DPCR, read32(DPCR) | 0x08);
            write32(DMA0_MADR, (uint32_t)dmabuf & 0xFFFFFF);
            write32(DMA0_BCR, ((NW[t] / bs) << 16) | bs);
            write32(MDEC_CMD, CMD[t]);
            zero();
            write32(DMA0_CHCR, 0x01000201);
            uint32_t td = 0, ti = 0, s = 0;
            for (;;) {
                const uint32_t c = now();
                if (!td && !(read32(DMA0_CHCR) & 0x01000000)) td = c;
                s = st();
                if (td && !(s & BUSY)) { ti = c; break; }
                if (c > 100000) { ti = 0xFFFFFFFFu; break; }
            }
            write32(DMA0_CHCR, 0);
            printf("  bs %2d: DMA0 %5d idle %5d", bs, (int)td, (int)ti);
        }
        printf("\n");
    }
    reset();

    printf("\nF: the upload, n nops (cached, warm), one status read: the first n reading idle (bit 29 clear)\n");
    stat_after(SLED); stat_after(SLED);
    for (int t = 0; t < 3; t++) {
        int first = -1; uint32_t sb = 0, sa = 0;
        for (int n = 0; n <= SLED; n++) {
            reset();
            upload(t, words[t], NW[t], 0);
            stat_after(SLED);                        /* warm the sled's lines again (the reset's wait may not) */
            reset();
            upload(t, words[t], NW[t], 0);
            const uint32_t s = stat_after(n);
            if (!(s & BUSY)) { first = n; sa = s; break; }
            sb = s;
        }
        if (first < 0) printf("  %s: busy through n %d (last %08x)\n", NAME[t], SLED, (int)sb);
        else printf("  %s: first idle at n %d (%08x; the n before %08x)\n", NAME[t], first, (int)sa, (int)sb);
    }

    ExitCriticalSection();
    restoreTimer(2, prevT2);
    mdec_reset();
    printf("\nmdec/mdec-tablebusy: Done.\n");
    for (;;) VSync(0);
}
