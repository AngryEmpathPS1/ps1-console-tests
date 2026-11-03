/*
 * mdec/mdec-reset13 - the reset behind K queued stack stores, with or without an on-die store last, the enables 9 or 12 cycles
 * behind it, over every phase of the refresh (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset11 (the SCPH-5501, byte-identical runs): two calls of mdec-reset's one() back to back lose the second's enables
 * in 1-2-cycle windows of the refresh, never with 200 cycles between and never when the first call made fewer stores. one()
 * puts its enables 9 cycles behind its reset (2 past the reset's 7 deaf cycles), behind CHCR0 = 0 and six stack saves - and the
 * first call's own last stores are still queued when a back-to-back call starts. So: does a deep queue, with a refresh in it,
 * hold the reset 3 cycles or more - and does it with an on-die store (CHCR0) last in the queue, which v0.377.261 says does not
 * hold it (mdec-reset's second table)? mdec-reset12 could not see it: its enables were 20 behind.
 *
 * WHAT IT DOES. Interrupts off, cached code (each body run once first). A body: K stores to the stack (K 0, 2, 4, 6), then
 * CHCR0 = 0 (T) or not (R), the reset, nops, the enables G cycles behind the reset (G 9 or 12). 256 tries a body, each from
 * another phase of Timer 2 (its 16 bits a whole number of refreshes); 400 cycles later a table command and the status's bit 28
 * (the enables kept). Lost tries by the phase the body began at, in bins of 16.
 *
 * READ IT LIKE THIS. Per body: lost of 256 and where. Losses at some phases with K large and none at K 0 is the queue and the
 * refresh holding the reset; T against R says whether an on-die store last stops that. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define CHCR0    0x1F801088u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* a0 = 1F800000h. K stack stores, CHCR0 = 0 or not, the reset (sw v0 at 1824h), G-1 cycles of nops, the enables (sw t2). */
#define BODY(K, T, G) \
    ".set push\n.set noreorder\n .align 4\n lui $a0, 0x1f80\n lui $v0, 0x8000\n lui $t2, 0x6000\n nop\n" \
    " .rept " #K "\n sw $zero, -64($sp)\n .endr\n" T " sw $v0, 0x1824($a0)\n .rept " #G " - 1\n nop\n .endr\n sw $t2, 0x1824($a0)\n.set pop\n"
#define TAIL_T " sw $zero, 0x1088($a0)\n"
#define TAIL_R ""
#define B(NAME, K, T, G) static __attribute__((noinline, aligned(16))) void NAME() { __asm__ volatile(BODY(K, T, G) ::: "a0", "v0", "t2", "memory"); }
B(r0g9, 0, TAIL_R, 9)  B(r2g9, 2, TAIL_R, 9)  B(r4g9, 4, TAIL_R, 9)  B(r6g9, 6, TAIL_R, 9)
B(t0g9, 0, TAIL_T, 9)  B(t2g9, 2, TAIL_T, 9)  B(t4g9, 4, TAIL_T, 9)  B(t6g9, 6, TAIL_T, 9)
B(r0g12, 0, TAIL_R, 12) B(r2g12, 2, TAIL_R, 12) B(r4g12, 4, TAIL_R, 12) B(r6g12, 6, TAIL_R, 12)
B(t0g12, 0, TAIL_T, 12) B(t2g12, 2, TAIL_T, 12) B(t4g12, 4, TAIL_T, 12) B(t6g12, 6, TAIL_T, 12)
typedef void (*Body)();
static const Body BODIES[16] = { r0g9, r2g9, r4g9, r6g9, t0g9, t2g9, t4g9, t6g9, r0g12, r2g12, r4g12, r6g12, t0g12, t2g12, t4g12, t6g12 };
static const char *NAMES[16] = { "R K0 G9", "R K2 G9", "R K4 G9", "R K6 G9", "T K0 G9", "T K2 G9", "T K4 G9", "T K6 G9",
                                 "R K0 G12", "R K2 G12", "R K4 G12", "R K6 G12", "T K0 G12", "T K2 G12", "T K4 G12", "T K6 G12" };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

static uint8_t lost[16][16];
static uint16_t lostN[16];

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset13\n");
    printf("The reset behind K stack stores, with or without CHCR0 = 0 last, the enables G behind it, over the refresh. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int b = 0; b < 16; b++) { settle(); BODIES[b](); }   /* cache them */
    for (int b = 0; b < 16; b++)
        for (int i = 0; i < 256; i++) {
            settle();
            waitCycles(50 + (uint32_t)(i * 37 % 256));
            const uint32_t phase = read16(T2_COUNT) & 0xFF;
            BODIES[b]();
            if (!kept()) { lostN[b]++; if (lost[b][phase / 16] < 255) lost[b][phase / 16]++; }
        }
    settle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("body       lost   by phase /16:  0  16  32  48  64  80  96 112 128 144 160 176 192 208 224 240\n");
    for (int b = 0; b < 16; b++) {
        printf("%-9s  %4d                ", NAMES[b], (int)lostN[b]);
        for (int k = 0; k < 16; k++) printf("%3d ", (int)lost[b][k]);
        printf("\n");
    }
    printf("\nmdec/mdec-reset13: Done.\n");
    for (;;) {}   /* park after Done. */
}
