/*
 * cpu/fuzz-bus - random CPU code with a DMA running under it: the whole bus at once, on nobody's chosen case (2026-11-01)
 *
 * WHY. The question put: "Why couldn't it cover the whole console?" hwtests/fuzz-cpu times random programs on a quiet bus and hwtests/fuzz-dma random transfers under a polling loop. A game is both at once: the CPU's
 * fills, reads and stores fighting a DMA's blocks and the refresh. This is the first of the tests that put them together.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc. fuzz-cpu's generator, 32 instructions, seeded by id. T2; the write that starts a DMA; the program; T2; then a loop polling the channel until it ends;
 * T2. Five kinds, each run warm (a run first) and cold, the fastest of 4: none, OTC of 256, GPU to VRAM 16 x 8, SPU RAM 16 x 4, and the GPU's linked list of 8 entries.
 *
 * READ IT LIKE THIS. B id kind warm warmEnd cold coldEnd hash: the program's time, the time the DMA was seen done, FNV of the registers and the sandbox after. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-bus --frames=30000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define PROGRAMS 400
#define LEN 16
#define TRIALS 4
#define KINDS 5
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define SPU_ADDR 0x1F801DA6u
#define SPU_CTRL 0x1F801DACu
#define SPUCNT 0x1F801DAAu
#define SPUSTAT 0x1F801DAEu
#define T2_COUNT 0x1F801120u
#define MADR(c) (0x1F801080u + (c) * 16u)
#define BCR(c) (0x1F801084u + (c) * 16u)
#define CHCR(c) (0x1F801088u + (c) * 16u)
#define WORDS 64

extern "C" {
uint32_t fzSandbox[0x2100] __attribute__((aligned(1024)));
uint32_t fzCode[32] __attribute__((aligned(16)));
uint32_t fzRegs[8];
uint32_t fzTime(uint32_t* code, uint32_t* regs, uint32_t chcrAddr, uint32_t chcrVal);
void fzEvict(void);
}

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".global fzEvict\n"
    "fzEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 64\n"
    ".global fzTime\n"
    "fzTime:\n"
    "addiu $sp, $sp, -24\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "sw $s3, 16($sp)\n"
    "lw $t0, 0($a1)\n"
    "lw $t1, 4($a1)\n"
    "lw $t2, 8($a1)\n"
    "lw $t3, 12($a1)\n"
    "lw $t4, 16($a1)\n"
    "lw $t5, 20($a1)\n"
    "lw $t6, 24($a1)\n"
    "lw $t7, 28($a1)\n"
    "lui $s0, %hi(fzSandbox)\n"
    "addiu $s0, $s0, %lo(fzSandbox)\n"
    "lui $s3, 1\n"
    "ori $s3, $s3, 0\n"
    "addiu $s3, $s3, -32768\n"
    "addu $s3, $s3, $s0\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "nop\n"
    "lhu $v0, 0x120($s2)\n"
    "sw $a3, 0($a2)\n"
    "jalr $a0\n"
    "nop\n"
    "lhu $v1, 0x120($s2)\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "lw $s3, 16($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 24\n"
    ".set pop\n"
);

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t R(uint32_t fn, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa) { return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn; }
static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
#define T() (8u + rnd() % 8u)
#define S0 16u
#define S1 17u
#define S2 18u
#define S3 19u
static const uint16_t IOW[4] = { 0x074, 0x0F0, 0x814, 0x044 };

static uint32_t gen1() {
    const uint32_t k = rnd() % 100;
    const uint32_t base = (rnd() & 1) ? S0 : S3;
    if (k < 20) return 0;
    if (k < 30) return R(0x21, T(), T(), T(), 0);
    if (k < 36) return R(0x26, T(), T(), T(), 0);
    if (k < 40) return R(0x00, 0, T(), T(), rnd() % 32);
    if (k < 44) return I(0x0D, T(), T(), rnd());
    if (k < 52) return I(0x23, base, T(), (rnd() % WORDS) * 4);
    if (k < 56) return I(0x25, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 59) return I(0x21, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 62) return I(0x24, base, T(), rnd() % (WORDS * 4));
    if (k < 64) return I(0x20, base, T(), rnd() % (WORDS * 4));
    if (k < 71) return I(0x2B, base, T(), (rnd() % WORDS) * 4);
    if (k < 74) return I(0x29, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 76) return I(0x28, base, T(), rnd() % (WORDS * 4));
    if (k < 79) return I(0x23, S1, T(), (rnd() % WORDS) * 4);
    if (k < 81) return I(0x2B, S1, T(), (rnd() % WORDS) * 4);
    if (k < 87) return I(0x23, S2, 0, IOW[rnd() % 4]);
    if (k < 90) return I(0x25, S2, 0, 0x110);
    if (k < 92) return I(0x25, S2, 0, 0xDAE);
    if (k < 95) return R(0x19, T(), T(), 0, 0);
    if (k < 96) return R(0x1B, T(), T(), 0, 0);
    if (k < 98) return R(0x10, 0, 0, T(), 0);
    return R(0x12, 0, 0, T(), 0);
}

static uint32_t full[LEN], master[WORDS * 2];
static void gen(int id) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    for (int i = 0; i < 8; i++) fzRegs[i] = rnd();
    for (int i = 0; i < WORDS * 2; i++) master[i] = rnd();
    for (int i = 0; i < LEN; i++) full[i] = gen1();
}
static void cut(int n) {
    for (int i = 0; i < n; i++) fzCode[i] = full[i];
    fzCode[n] = 0x03E00008;
    fzCode[n + 1] = 0;
}
static void sandboxIn() {
    for (int i = 0; i < WORDS; i++) { fzSandbox[i] = master[i]; fzSandbox[0x2000 + i] = master[WORDS + i]; }
    volatile uint32_t* sp = (volatile uint32_t*)0x1F800000u;
    for (int i = 0; i < WORDS; i++) sp[i] = master[i] ^ 0x5A5A5A5Au;
}
static void spin(int n) { for (volatile int i = 0; i < n; i++) {} }
static uint32_t dmaBuf[512] __attribute__((aligned(16)));
static int timeouts;
static void gpuReady() { for (int i = 0; i < 400000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); }
static void spuMode(uint32_t mode) {
    write16(SPUCNT, (uint16_t)(0xC000u | (mode << 4)));
    for (int i = 0; i < 4000; i++) if ((read16(SPUSTAT) & 0x30u) == (mode << 4)) return;
    timeouts++; printf("TIMEOUT spu %x\n", read16(SPUSTAT));
}
static int chOf(int k) { return k == 1 ? 6 : k == 3 ? 4 : 2; }
static uint32_t chcrOf(int k) { return k == 0 ? 0 : k == 1 ? 0x11000002u : k == 4 ? 0x01000401u : 0x01000201u; }
static void arm(int k) {
    if (k == 1) { write32(MADR(6), (uint32_t)&dmaBuf[255] & 0xFFFFFCu); write32(BCR(6), 256); }
    else if (k == 2) { gpuReady(); write32(GP1, 0x04000002u); write32(GP0, 0xA0000000u); write32(GP0, (300u << 16) | 0u); write32(GP0, (1u << 16) | 256u); write32(MADR(2), (uint32_t)dmaBuf & 0xFFFFFCu); write32(BCR(2), (8u << 16) | 16u); }
    else if (k == 3) { spuMode(0); write16(SPU_ADDR, 0x400); spuMode(2); write32(MADR(4), (uint32_t)dmaBuf & 0xFFFFFCu); write32(BCR(4), (4u << 16) | 16u); }
    else if (k == 4) {
        gpuReady(); write32(GP1, 0x04000002u);
        for (int e = 0; e < 8; e++) { dmaBuf[e * 4] = (3u << 24) | (e == 7 ? 0xFFFFFFu : ((uint32_t)&dmaBuf[(e + 1) * 4] & 0xFFFFFCu)); dmaBuf[e * 4 + 1] = 0; dmaBuf[e * 4 + 2] = 0; dmaBuf[e * 4 + 3] = 0; }
        write32(MADR(2), (uint32_t)dmaBuf & 0xFFFFFCu); write32(BCR(2), 0);
    }
}
static uint32_t waitEnd(int k) {
    if (k) for (int i = 0; i < 400000; i++) if (!(read32(CHCR(chOf(k))) & 0x01000000u)) break;
    return read16(T2_COUNT);
}
static uint32_t hashOut() {
    uint32_t h = 2166136261u;
    for (int i = 0; i < WORDS; i++) { h ^= fzSandbox[i]; h *= 16777619u; h ^= fzSandbox[0x2000 + i]; h *= 16777619u; }
    volatile uint32_t* sp = (volatile uint32_t*)0x1F800000u;
    for (int i = 0; i < WORDS; i++) { h ^= sp[i]; h *= 16777619u; }
    return h;
}

int main() {
    printf("\ncpu/fuzz-bus\n");
    printf("%d programs x %d kinds of DMA: B id kind warm warmEnd cold coldEnd hash. No assertions.\n\n", PROGRAMS, KINDS);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR);
    write32(DPCR, dp0 | (1u << 11) | (1u << 19) | (1u << 27));
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write16(SPU_CTRL, 4);
    for (int i = 0; i < 512; i++) dmaBuf[i] = 0;
    for (int id = 0; id < PROGRAMS; id++) {
        gen(id);
        cut(LEN);
        for (int k = 0; k < KINDS; k++) {
            const uint32_t addr = k ? CHCR(chOf(k)) : 0x1F800300u, val = chcrOf(k);
            uint32_t wm = 0xFFFF, we = 0, cm = 0xFFFF, ce = 0, h = 0;
            for (int t = 0; t < TRIALS; t++) {
                spin(id % 5 + t * 13);
                sandboxIn(); fzEvict();
                sandboxIn();
                arm(k); fzTime(fzCode, fzRegs, addr, val); waitEnd(k);
                sandboxIn();
                arm(k);
                const uint16_t t0 = read16(T2_COUNT);
                const uint32_t w = fzTime(fzCode, fzRegs, addr, val);
                const uint32_t e = (uint16_t)(waitEnd(k) - t0);
                if (w < wm) { wm = w; we = e; h = hashOut(); }
            }
            for (int t = 0; t < TRIALS; t++) {
                spin(id % 7 + t * 11);
                sandboxIn();
                arm(k);
                fzEvict();
                const uint16_t t0 = read16(T2_COUNT);
                const uint32_t c = fzTime(fzCode, fzRegs, addr, val);
                const uint32_t e = (uint16_t)(waitEnd(k) - t0);
                if (c < cm) { cm = c; ce = e; }
            }
            printf("B %d %d %u %u %u %u %x\n", id, k, wm, we, cm, ce, h);
        }
    }
    spuMode(0);
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ncpu/fuzz-bus: Done.\n", timeouts);
    for (;;) VSync(0);
}
