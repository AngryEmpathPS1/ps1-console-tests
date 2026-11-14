/*
 * mdec/mdec-tablebusy2 - the MDEC's busy bit after a table written by BACK-TO-BACK stores, to the cycle.
 *
 * WHY (2026-11-14; STATUS "mdec-tablebusy"). hwtests/mdec-tablebusy wrote each table word from a C loop, ~15 cycles apart, and
 * found the MDEC idle at the first read after the last word on the SCPH-7502 and the PSone, as this core is. The console
 * fuzzer's 4001-6 writes them ~3 cycles apart (lui / ori / sw) and reads busy (A004FFFFh) where this core reads 8004FFFFh. So
 * the MDEC may take a table word slower than stores can bring them, and drain after the last.
 *
 * WHAT IT DOES. A 32-word table (quant colour, command 40000001h; scale, command 60000000h; every word 01010101h - mdec-tablebusy
 * Z: the content does not matter) written by 33 stores from one register with G nops between (G = 0, 1, 2, 4, 8), then n nops and
 * one status read; all in cached code, run warm (each call made twice, the first to fill the I-cache). For each G the first n
 * whose read has bit 29 clear (n = 0 .. 400), and the status there and just before. Also the time of the 33 stores themselves
 * (Timer 2 at the system clock, bracketing the call with n = 0).
 *
 * READ IT LIKE THIS. A first-idle n that falls as G rises says the MDEC consumes a word every P cycles: the last word is taken
 * about 33 P after the first store, and the stores take 33 (G + s). Compare against this core's own scan.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define MDEC_STAT 0x1F801824
#define BUSY      (1u << 29)
#define SLED      400

extern "C" {
uint32_t up0(uint32_t cmd, uint32_t word, uint32_t n);
uint32_t up1(uint32_t cmd, uint32_t word, uint32_t n);
uint32_t up2(uint32_t cmd, uint32_t word, uint32_t n);
uint32_t up4(uint32_t cmd, uint32_t word, uint32_t n);
uint32_t up8(uint32_t cmd, uint32_t word, uint32_t n);
}
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".macro UPF name, gap\n"
    ".balign 16\n.global \\name\n"
    "\\name:\n"
    "  lui   $t1, 0x1f80\n"
    "  la    $t0, tb_sled_end\n"
    "  sll   $t2, $a2, 2\n"
    "  subu  $t0, $t0, $t2\n"
    "  sw    $a0, 0x1820($t1)\n"
    "  .rept \\gap\n  nop\n  .endr\n"
    "  .rept 32\n"
    "  sw    $a1, 0x1820($t1)\n"
    "  .rept \\gap\n  nop\n  .endr\n"
    "  .endr\n"
    "  jr    $t0\n"
    "  nop\n"
    ".endm\n"
    "UPF up0, 0\n"
    "UPF up1, 1\n"
    "UPF up2, 2\n"
    "UPF up4, 4\n"
    "UPF up8, 8\n"
    ".balign 16\n"
    "tb_sled:\n"
    "  .rept 400\n  nop\n  .endr\n"
    "tb_sled_end:\n"
    "  lui   $t2, 0x1f80\n"
    "  lw    $v0, 0x1824($t2)\n"
    "  jr    $ra\n"
    "  nop\n"
    ".set pop\n");

typedef uint32_t (*Up)(uint32_t, uint32_t, uint32_t);
static const Up UP[5] = { up0, up1, up2, up4, up8 };
static const int GAP[5] = { 0, 1, 2, 4, 8 };
static const uint32_t CMD[2] = { 0x40000001u, 0x60000000u };
static const char* const NAME[2] = { "quant color", "scale      " };

static uint32_t clk; static uint16_t last;
static inline uint32_t now() { const uint16_t t = readTimer(2); clk += (uint16_t)(t - last); last = t; return clk; }
static void waitc(uint32_t g) { clk = 0; last = readTimer(2); while (now() < g) {} }
static inline uint32_t st() { return read32(MDEC_STAT); }
static void reset() { write32(MDEC_STAT, 1u << 31); st(); st(); st(); st(); write32(MDEC_STAT, 0); waitc(2000); }

int main() {
    printf("\nmdec/mdec-tablebusy2\n");
    printf("The MDEC's busy bit after a table of back-to-back stores, to the cycle. No assertions - compare against your emulator.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    for (int t = 0; t < 2; t++) {
        for (int g = 0; g < 5; g++) {
            const Up up = UP[g];
            /* the stores' own time: the call with n = 0, bracketed by two timer reads (warm) */
            reset(); up(CMD[t], 0x01010101u, SLED);
            reset();
            const uint16_t a = readTimer(2);
            up(CMD[t], 0x01010101u, 0);
            const uint16_t b = readTimer(2);
            int first = -1; uint32_t sa = 0, sb = 0;
            for (int n = 0; n <= SLED; n++) {
                reset(); up(CMD[t], 0x01010101u, SLED);   /* warm: the function and the whole sled */
                reset();
                const uint32_t s = up(CMD[t], 0x01010101u, n);
                if (!(s & BUSY)) { first = n; sa = s; break; }
                sb = s;
            }
            reset();
            if (first < 0) printf("  %s G %d: call %4d; busy through n %d (%08x)\n", NAME[t], GAP[g], (int)(uint16_t)(b - a), SLED, (int)sb);
            else printf("  %s G %d: call %4d; first idle n %3d (%08x; before %08x)\n", NAME[t], GAP[g], (int)(uint16_t)(b - a), first, (int)sa, (int)sb);
        }
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("\nmdec/mdec-tablebusy2: Done.\n");
    for (;;) VSync(0);
}
