/*
 * gpu/c0-cold - Datel's StoreImage check in code that misses the instruction cache, at every place in the line
 * (2026-10-10)
 *
 * WHY. Datel's carts (GameShark 2.41/2.42, Action Replay 2.8/2.81, Pro Action Replay 2, GameShark Pro 3.2) issue a
 * GP0(C0h) VRAM-to-CPU copy and then read GPUSTAT once: if bit 27 ("ready to send VRAM to CPU") is ALREADY set they wait
 * for it to clear, which it never does, and the cart hangs (STATUS M333; src/psx.js GPU_C0_SETUP). Every cart works on a
 * console, so their one read always comes before the bit rises - and it comes from code that has just missed the cache,
 * right behind four stores to the GPU, each through a pointer the line before loaded from RAM. GameShark 2.42 has the
 * size store at word 0 of its line, GameShark Pro 3.2 the same routine two words later. A model of cold code
 * (hwtests/load-fill..4) that puts the GPU's stores and the dependent loads on the bus in arrival order must still get
 * that read in before the bit: this times it on a console.
 *
 * WHAT IT DOES. The routine as the carts have it (GameShark 2.42 80010CCCh-80010D40h, its lui's the pointers' base),
 * GPU wait loop to the GPUSTAT read, then a delay and one GPUREAD read to finish the 2x1 copy. Copies laid end to end
 * (12 KB, three times the cache), each run once a pass, so each is cold. A row is the size store's word in its line
 * (0-3) and k nops more before the GPUSTAT read (0-3); four copies a row, eight passes: how many of 32 reads saw bit 27
 * set.
 *
 * READ IT LIKE THIS. The carts' own rows are word 0 and word 2 at k = 0. A row that reads set most of the time is a
 * cart that hangs, so those two should read 0 or close to it (GameShark 2.42's own check found it set 0 or 1 time in 16,
 * console-logs/2026-10-05). The k at which each row turns is the margin.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-c0-cold`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

uint32_t ptrs[4] = { 0x1F801814u, 0x1F801814u, 0x1F801810u, 0 };

#define N(n) ".rept " #n "\nnop\n.endr\n"
/* GameShark 2.42 80010CCCh..80010D3Ch, `lui v0/a0, 8001h` as `addu v0/a0, s0, zero` (the pointers' base) */
#define R_HEAD \
  "addu $v0, $s0, $zero\n" "lw $v1, 0($v0)\n" "lui $a0, 0x400\n" \
  "1: lw $v0, 0($v1)\n" "nop\n" "and $v0, $v0, $a0\n" "beqz $v0, 1b\n" "addu $v0, $s0, $zero\n" \
  "lw $v1, 4($v0)\n" "lui $v0, 0x400\n" "addu $a0, $s0, $zero\n" "sw $v0, 0($v1)\n" \
  "lw $v1, 8($a0)\n" "lui $v0, 0x100\n" "sw $v0, 0($v1)\n" \
  "lw $v1, 8($a0)\n" "lui $v0, 0xc000\n" "sw $v0, 0($v1)\n" \
  "sll $v0, $a1, 16\n" "lw $v1, 8($a0)\n" "or $v0, $v0, $t2\n" "sw $v0, 0($v1)\n" \
  "sll $v0, $a3, 16\n" "lw $v1, 8($a0)\n" "or $v0, $v0, $a2\n" "sw $v0, 0($v1)\n" \
  "addu $v0, $s0, $zero\n" "lw $a0, 0($v0)\n" "nop\n"
/* the read, its bit counted; ~250 cycles; one GPUREAD read takes the copy's word */
#define R_TAIL \
  "lw $v0, 0($a0)\n" "nop\n" "srl $v0, $v0, 27\n" "andi $v0, $v0, 1\n" "addu $t8, $t8, $v0\n" \
  "li $t9, 64\n" "2: addiu $t9, $t9, -1\n" "bnez $t9, 2b\n" "nop\n" \
  "lw $v1, 8($s0)\n" "nop\n" "lw $t9, 0($v1)\n" "nop\n"

/* the size store at word (PAD + 25) & 3 = (PAD + 1) & 3 of its line */
#define ROW(NAME, PAD, K) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t base) { \
    uint32_t r; \
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nmove $s0, %1\nli $a1, 0\nli $t2, 0\nli $a3, 1\nli $a2, 2\nli $t8, 0\n" \
                     ".rept 4\n.balign 64\n" N(PAD) R_HEAD N(K) R_TAIL ".endr\n" \
                     "move %0, $t8\n.set pop\n" \
                     : "=&r"(r) : "r"(base) : "s0", "a0", "a1", "a2", "a3", "v0", "v1", "t2", "t8", "t9", "memory"); \
    return r; }

ROW(w0k0, 3, 0) ROW(w0k1, 3, 1) ROW(w0k2, 3, 2) ROW(w0k3, 3, 3)
ROW(w1k0, 0, 0) ROW(w1k1, 0, 1) ROW(w1k2, 0, 2) ROW(w1k3, 0, 3)
ROW(w2k0, 1, 0) ROW(w2k1, 1, 1) ROW(w2k2, 1, 2) ROW(w2k3, 1, 3)
ROW(w3k0, 2, 0) ROW(w3k1, 2, 1) ROW(w3k2, 2, 2) ROW(w3k3, 2, 3)

typedef uint32_t (*Row)(uint32_t);
static const Row ROWS[16] = { w0k0, w0k1, w0k2, w0k3, w1k0, w1k1, w1k2, w1k3, w2k0, w2k1, w2k2, w2k3, w3k0, w3k1, w3k2, w3k3 };

int main() {
    printf("\ngpu/c0-cold\n");
    printf("Datel's StoreImage check (GP0 C0h, then one GPUSTAT read) in cold code: the size store at word w of its\n");
    printf("line, k more nops before the read; of 32 reads, how many saw bit 27 (ready to send) already set.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    uint32_t set[16] = { 0 };
    for (int pass = 0; pass < 8; pass++)
        for (int i = 0; i < 16; i++) set[i] += ROWS[i]((uint32_t)&ptrs[0]);
    ExitCriticalSection();
    for (int w = 0; w < 4; w++) {
        printf("  size store at word %d:", w);
        for (int k = 0; k < 4; k++) printf("   k=%d %2u/32", k, (unsigned)set[w * 4 + k]);
        printf("\n");
    }
    printf("\nDone.\n");
    for (;;) {}
}
