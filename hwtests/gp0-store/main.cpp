/*
 * gpu/gp0-store - how long the CPU waits behind its own stores to GP0 (2026-10-14 small hours).
 *
 * WHY. gpu-cost-sweep and gpu-thin poll GPUSTAT after writing a primitive from the CPU, and on the SCPH-7501 their first
 * poll lands ~50 cycles later than this core's for every 3-word command (a 1x1 rectangle: bits 28 and 26 both first read
 * at 140 cycles from its first word, here 91 and 141), 4-word ones the same (157 against 107), and ~265 later behind an
 * 8-bit textured rectangle that reloads its CLUT (398 against 134) - while the exact method (gpu-shape-time2: the words,
 * D cycles, ONE GPUSTAT read) puts the console's bit 28 where this core does. So the read behind the stores waits: for
 * the GPU to take the words? for the CLUT? This times it with nothing but Timer 2.
 *
 * WHAT IT DOES. Cached code, interrupts off, Timer 2 at the system clock; each row the two Timer 2 reads' distance,
 * fastest..slowest of 16 over the refresh, the GPU idle before each (GPUSTAT 26 and 28 set, then a spin).
 *   A n    lhu T2; n stores of GP0(00h) (a NOP word) back to back; lhu T2 - n = 0, 1, 2, 3, 4, 6, 8, 12, 16
 *   B k    lhu T2; 3 NOP stores; k nops; lhu T2 - k = 0, 4, 8, 16, 24, 32, 48 (the read's wait is the total less 3 + k)
 *   C      lhu T2; 3 NOP stores; lw GPUSTAT; lhu T2 - a GPUSTAT read behind them, as a poll's first
 *   R      lhu T2; a 1x1 flat rectangle (60h, 3 words, at 0,300); lhu T2
 *   RS     lhu T2; the same rectangle; lw GPUSTAT; lhu T2
 *   T k    lhu T2; an 8-bit textured 16x16 rectangle (64h, 4 words) whose CLUT is not the one cached (two CLUT rows taken
 *          in turn); k nops; lhu T2 - k = 0, 32, 128, 256, 384: does the read wait for the CLUT's 256 entries?
 *   TC     the same with its CLUT the one cached (it was the last drawn)
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gp0-store --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

/* t9 = 1F800000h; t7 = the textured rectangle's CLUT/UV word (set by the caller); each returns its count in v0 */
#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define FN(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body \
            "nop\nsubu $v0, $t2, $t1\njr $ra\nandi $v0, $v0, 0xffff\n.set pop\n");
#define ST(N) ".rept " #N "\nsw $zero, 0x1810($t9)\n.endr\n"
#define AROW(N) FN(a##N, "lhu $t1, 0x1120($t9)\n" ST(N) "lhu $t2, 0x1120($t9)\n")
FN(a0, "lhu $t1, 0x1120($t9)\nlhu $t2, 0x1120($t9)\n")
AROW(1) AROW(2) AROW(3) AROW(4) AROW(6) AROW(8) AROW(12) AROW(16)
#define BROW(K) FN(b##K, "lhu $t1, 0x1120($t9)\n" ST(3) NOPS(K) "lhu $t2, 0x1120($t9)\n")
FN(b0, "lhu $t1, 0x1120($t9)\n" ST(3) "lhu $t2, 0x1120($t9)\n")
BROW(4) BROW(8) BROW(16) BROW(24) BROW(32) BROW(48)
FN(c0, "lhu $t1, 0x1120($t9)\n" ST(3) "lw $t3, 0x1814($t9)\nlhu $t2, 0x1120($t9)\n")
/* the 1x1 rectangle: 60h colour 404040h, at (0,300), size 1x1 */
#define RECT "lui $t3, 0x6040\nori $t3, $t3, 0x4040\nlui $t4, 300\nlui $t5, 1\nori $t5, $t5, 1\n"
FN(r0, RECT "lhu $t1, 0x1120($t9)\nsw $t3, 0x1810($t9)\nsw $t4, 0x1810($t9)\nsw $t5, 0x1810($t9)\nlhu $t2, 0x1120($t9)\n")
FN(rs0, RECT "lhu $t1, 0x1120($t9)\nsw $t3, 0x1810($t9)\nsw $t4, 0x1810($t9)\nsw $t5, 0x1810($t9)\nlw $t6, 0x1814($t9)\nlhu $t2, 0x1120($t9)\n")
/* the 8-bit textured 16x16 rectangle: 64h colour 808080h, at (32,300), CLUT/UV from $t7, size 16x16 */
#define TRECT "lui $t3, 0x6480\nori $t3, $t3, 0x8080\nlui $t4, 300\nori $t4, $t4, 32\nlui $t5, 16\nori $t5, $t5, 16\n"
#define TROW(K) FN(t##K, TRECT "lhu $t1, 0x1120($t9)\nsw $t3, 0x1810($t9)\nsw $t4, 0x1810($t9)\nsw $t7, 0x1810($t9)\nsw $t5, 0x1810($t9)\n" NOPS(K) "lhu $t2, 0x1120($t9)\n")
FN(t0, TRECT "lhu $t1, 0x1120($t9)\nsw $t3, 0x1810($t9)\nsw $t4, 0x1810($t9)\nsw $t7, 0x1810($t9)\nsw $t5, 0x1810($t9)\nlhu $t2, 0x1120($t9)\n")
TROW(32) TROW(128) TROW(256) TROW(384)

extern "C" uint32_t callRow(uint32_t (*f)(void), uint32_t t7);
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callRow\ncallRow:\nmove $t8, $ra\nlui $t9, 0x1f80\nmove $t7, $a1\njalr $a0\nnop\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

static void idle() {
    for (uint32_t g = 0; g < 2000000u && (read32(0x1F801814u) & 0x14000000u) != 0x14000000u; g++) {}
    spin(400);
}
/* two CLUT rows at Y 480 and 481 (x 0): taken in turn, the cache always reloads; tc: the same row twice */
static const uint32_t CLUT_A = (480u << 22), CLUT_B = (481u << 22);
typedef uint32_t (*Row)(void);
static void row(const char* name, Row f, int clutMode) {
    uint32_t lo = 0xFFFF, hi = 0, flip = 0;
    for (uint32_t k = 0; k < 17; k++) {
        idle(); spin(k * 8);
        const uint32_t cl = clutMode == 1 ? ((flip++ & 1) ? CLUT_B : CLUT_A) : CLUT_A;
        if (clutMode == 2) { callRow(f, cl); idle(); spin(k * 8); }   /* TC: draw once so its CLUT is the cached one */
        const uint32_t v = callRow(f, cl);
        if (k == 0) continue;   /* the first warms the code */
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    printf("  %-5s %4u..%-4u\n", name, (unsigned)lo, (unsigned)hi);
}

int main() {
    printf("\ngpu/gp0-store\n");
    printf("The CPU behind its own GP0 stores (cached; Timer 2 cycles, fastest..slowest of 16 over the refresh).\n");
    printf("A n: lhu T2; n NOP stores to GP0; lhu T2.  B k: 3 NOP stores, k nops.  C: 3 NOP stores, a GPUSTAT read.\n");
    printf("R: a 1x1 rectangle (3 words).  RS: and a GPUSTAT read.  T k: an 8-bit 16x16 textured rectangle reloading its\n");
    printf("CLUT, k nops.  TC: its CLUT cached. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    idle();
    write32(0x1F801810u, 0xE1000400u | 8u | (1u << 7));   /* texpage X 512 Y 0, 8-bit, dither off, drawing to display on */
    write32(0x1F801810u, 0xE3000000u); write32(0x1F801810u, 0xE4000000u | (511u << 10) | 1023u); write32(0x1F801810u, 0xE5000000u);
    idle();
    row("A0", a0, 0); row("A1", a1, 0); row("A2", a2, 0); row("A3", a3, 0); row("A4", a4, 0);
    row("A6", a6, 0); row("A8", a8, 0); row("A12", a12, 0); row("A16", a16, 0);
    row("B0", b0, 0); row("B4", b4, 0); row("B8", b8, 0); row("B16", b16, 0); row("B24", b24, 0); row("B32", b32, 0); row("B48", b48, 0);
    row("C", c0, 0);
    row("R", r0, 0); row("RS", rs0, 0);
    row("T0", t0, 1); row("T32", t32, 1); row("T128", t128, 1); row("T256", t256, 1); row("T384", t384, 1);
    row("TC", t0, 2);
    idle();
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ngpu/gp0-store: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
