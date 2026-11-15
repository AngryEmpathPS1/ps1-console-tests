/*
 * mdec/mdec-storerate - 33 back-to-back stores to an on-die register: the MDEC's command register, Timer 0's target, and RAM.
 *
 * WHY (2026-11-14; STATUS "mdec-tablebusy"). hwtests/mdec-tablebusy2: 33 MDEC stores with no instruction between take 175 cycles
 * on the second 5501, the 7502 and the PSone and 134 on this core (v0.377.348), and at 0 or 1 nops between them the read right
 * after the last still sees the MDEC busy (A004FFFFh), where this core reads idle; at 2 or more nops both agree. MDEC_ST_LEN does
 * not move the 0-nop row (2, 3, 4 all read 134): back-to-back stores take this core's store-stream path. This asks whether the
 * gap is the MDEC's or every on-die store's.
 *
 * WHAT IT DOES. Cached code, run warm (each call twice, the second timed by two Timer 2 reads at the system clock): the MDEC reset
 * then the scale-table command (60000000h) and 32 words of 01010101h to 1F801820h; 33 stores of 0 to Timer 0's target 1F801108h
 * (the fuzz host's own register); 33 stores to a RAM buffer. Each with G = 0, 1, 2, 4 nops between, then a load of the same
 * place's status (MDEC: its status 1F801824h; Timer 0: 1F801104h; RAM: the buffer) so the time includes the drain.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

extern "C" {
uint32_t m0(uint32_t), m1(uint32_t), m2(uint32_t), m4(uint32_t);
uint32_t t0s(uint32_t), t1s(uint32_t), t2s(uint32_t), t4s(uint32_t);
uint32_t r0(uint32_t), r1(uint32_t), r2(uint32_t), r4(uint32_t);
}
static uint32_t ramb[64] __attribute__((aligned(16)));
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".macro SR name, gap, base, off, roff, first\n"
    ".balign 16\n.global \\name\n"
    "\\name:\n"
    "  \\base\n"
    "  li    $a1, 0x01010101\n"
    "  .if \\first\n  sw $a0, \\off($t1)\n  .else\n  sw $zero, \\off($t1)\n  .endif\n"
    "  .rept \\gap\n  nop\n  .endr\n"
    "  .rept 32\n"
    "  .if \\first\n  sw $a1, \\off($t1)\n  .else\n  sw $zero, \\off($t1)\n  .endif\n"
    "  .rept \\gap\n  nop\n  .endr\n"
    "  .endr\n"
    "  lw    $v0, \\roff($t1)\n"
    "  jr    $ra\n"
    "  nop\n"
    ".endm\n"
    "SR m0, 0, \"lui $t1, 0x1f80\", 0x1820, 0x1824, 1\n"
    "SR m1, 1, \"lui $t1, 0x1f80\", 0x1820, 0x1824, 1\n"
    "SR m2, 2, \"lui $t1, 0x1f80\", 0x1820, 0x1824, 1\n"
    "SR m4, 4, \"lui $t1, 0x1f80\", 0x1820, 0x1824, 1\n"
    "SR t0s, 0, \"lui $t1, 0x1f80\", 0x1108, 0x1104, 0\n"
    "SR t1s, 1, \"lui $t1, 0x1f80\", 0x1108, 0x1104, 0\n"
    "SR t2s, 2, \"lui $t1, 0x1f80\", 0x1108, 0x1104, 0\n"
    "SR t4s, 4, \"lui $t1, 0x1f80\", 0x1108, 0x1104, 0\n"
    "SR r0, 0, \"move $t1, $a0\", 0, 0, 0\n"
    "SR r1, 1, \"move $t1, $a0\", 0, 0, 0\n"
    "SR r2, 2, \"move $t1, $a0\", 0, 0, 0\n"
    "SR r4, 4, \"move $t1, $a0\", 0, 0, 0\n"
    ".set pop\n");

typedef uint32_t (*F)(uint32_t);
static const F FN[3][4] = { { m0, m1, m2, m4 }, { t0s, t1s, t2s, t4s }, { r0, r1, r2, r4 } };
static const char* const NAME[3] = { "MDEC 1F801820h ", "Timer0 target  ", "RAM            " };
static const int GAP[4] = { 0, 1, 2, 4 };

static void mreset() { write32(0x1F801824, 1u << 31); read32(0x1F801824); read32(0x1F801824); read32(0x1F801824); write32(0x1F801824, 0); for (volatile int i = 0; i < 200; i++) {} }

int main() {
    printf("\nmdec/mdec-storerate\n");
    printf("33 back-to-back stores to an on-die register or RAM, warm (Timer 2, system clock). No assertions - compare against your emulator.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    for (int k = 0; k < 3; k++) {
        printf("  %s", NAME[k]);
        for (int g = 0; g < 4; g++) {
            const uint32_t arg = k == 0 ? 0x60000000u : k == 2 ? ((uint32_t)ramb) : 0;
            uint32_t best = 0xFFFFFFFFu, worst = 0, s = 0;
            for (int r = 0; r < 4; r++) {
                if (k == 0) mreset();
                FN[k][g](arg);                       /* warm */
                if (k == 0) mreset();
                const uint16_t a = readTimer(2);
                s = FN[k][g](arg);
                const uint16_t b = readTimer(2);
                const uint32_t d = (uint16_t)(b - a);
                if (d < best) best = d; if (d > worst) worst = d;
            }
            printf("  G %d: %4d..%4d", GAP[g], (int)best, (int)worst);
            if (k == 0) printf(" (%08x)", (int)s);
        }
        printf("\n");
    }
    write32(0x1F801108, 0);
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    mreset();
    printf("\nmdec/mdec-storerate: Done.\n");
    for (;;) VSync(0);
}
