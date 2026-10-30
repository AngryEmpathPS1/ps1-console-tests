/*
 * gpu/c0-cold2 - Datel's StoreImage check: does the copy's size, or a warm cache, decide it? (2026-10-10)
 *
 * WHY. hwtests/gpu-c0-cold (the SCPH-9001, three runs identical) ran Datel's routine on a 2x1 copy in cold code: with
 * the size store at word 2 of its line the GPUSTAT read beat bit 27 every time, at word 0 or 1 it lost 29-31 times in
 * 32, at word 3 it beat it only with no extra nop. GameShark 2.42's routine has the size store at word 0 - and the cart
 * works on a console. Its boot copy is 1024x512 (VRAM's whole width and height), not 2x1, and the routine may run from a
 * warm cache. This asks which.
 *
 * WHAT IT DOES. The same routine (GameShark 2.42 80010CCCh-80010D40h), the size store at word 0, k = 0:
 *   COLD at four sizes - 2x1, 64x2, 1024x2, 1024x512 - each copy drained by as many GPUREAD reads as it has words;
 *   WARM at the four word places, 2x1: the routine twice in a loop, only the second (cached) read counted.
 * 16 reads a row (cold: 4 copies a pass, 4 passes; 1024x512 one copy a pass, 8 passes).
 *
 * READ IT LIKE THIS. A row near 0 is a read that beats bit 27 (the cart goes on), near its total a cart that hangs.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-c0-cold2`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

uint32_t ptrs[4] = { 0x1F801814u, 0x1F801814u, 0x1F801810u, 0 };

#define N(n) ".rept " #n "\nnop\n.endr\n"
#define R_HEAD \
  "addu $v0, $s0, $zero\n" "lw $v1, 0($v0)\n" "lui $a0, 0x400\n" \
  "1: lw $v0, 0($v1)\n" "nop\n" "and $v0, $v0, $a0\n" "beqz $v0, 1b\n" "addu $v0, $s0, $zero\n" \
  "lw $v1, 4($v0)\n" "lui $v0, 0x400\n" "addu $a0, $s0, $zero\n" "sw $v0, 0($v1)\n" \
  "lw $v1, 8($a0)\n" "lui $v0, 0x100\n" "sw $v0, 0($v1)\n" \
  "lw $v1, 8($a0)\n" "lui $v0, 0xc000\n" "sw $v0, 0($v1)\n" \
  "sll $v0, $a1, 16\n" "lw $v1, 8($a0)\n" "or $v0, $v0, $t2\n" "sw $v0, 0($v1)\n" \
  "sll $v0, $a3, 16\n" "lw $v1, 8($a0)\n" "or $v0, $v0, $a2\n" "sw $v0, 0($v1)\n" \
  "addu $v0, $s0, $zero\n" "lw $a0, 0($v0)\n" "nop\n"
/* the read, its bit counted (t8, or into t7 when t6 says so); ~250 cycles; then t5 GPUREAD reads drain the copy */
#define R_TAIL \
  "lw $v0, 0($a0)\n" "nop\n" "srl $v0, $v0, 27\n" "andi $v0, $v0, 1\n" "addu $t8, $t8, $v0\n" \
  "li $t9, 64\n" "2: addiu $t9, $t9, -1\n" "bnez $t9, 2b\n" "nop\n" \
  "lw $v1, 8($s0)\n" "move $t9, $t5\n" "3: lw $t4, 0($v1)\n" "addiu $t9, $t9, -1\n" "bnez $t9, 3b\n" "nop\n"

/* cold: COPIES copies, the size store at word 0 (PAD 3); H, W the copy; WORDS = W * H / 2 */
#define COLD(NAME, COPIES, W, H) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t base) { \
    uint32_t r; \
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nmove $s0, %1\nli $a1, 0\nli $t2, 0\nli $a3, " #H "\nli $a2, " #W "\nli $t8, 0\nli $t5, (" #W "*" #H "/2)\n" \
                     ".rept " #COPIES "\n.balign 64\n" N(3) R_HEAD R_TAIL ".endr\n" \
                     "move %0, $t8\n.set pop\n" \
                     : "=&r"(r) : "r"(base) : "s0", "a0", "a1", "a2", "a3", "v0", "v1", "t2", "t4", "t5", "t8", "t9", "memory"); \
    return r; }
/* warm: the routine twice in a loop (t6 counts down); the first read's bit dropped, the second's kept */
#define WARM(NAME, PAD) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t base) { \
    uint32_t r; \
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nmove $s0, %1\nli $a1, 0\nli $t2, 0\nli $a3, 1\nli $a2, 2\nli $t5, 1\nli $t6, 2\n" \
                     ".balign 64\n" N(PAD) "4: li $t8, 0\n" R_HEAD R_TAIL "addiu $t6, $t6, -1\nbnez $t6, 4b\nnop\n" \
                     "move %0, $t8\n.set pop\n" \
                     : "=&r"(r) : "r"(base) : "s0", "a0", "a1", "a2", "a3", "v0", "v1", "t2", "t4", "t5", "t6", "t8", "t9", "memory"); \
    return r; }

COLD(c2x1, 4, 2, 1) COLD(c64x2, 4, 64, 2) COLD(c1024x2, 4, 1024, 2) COLD(c1024x512, 1, 1024, 512)
/* warm: "4: li t8" sits one word before R_HEAD, so the size store is at word (PAD + 26) & 3 */
WARM(w0, 2) WARM(w1, 3) WARM(w2, 0) WARM(w3, 1)
/* 12 KB of padding run between rows so the cold rows start cold */
static void __attribute__((noinline, aligned(64))) flush() {
    __asm__ volatile(".set push\n.set noreorder\n.rept 3072\nnop\n.endr\n.set pop\n");
}

int main() {
    printf("\ngpu/c0-cold2\n");
    printf("Datel's StoreImage check: the copy's size (cold, size store at word 0) and a warm cache (2x1, each word).\n");
    printf("How many reads saw bit 27 (ready to send) already set. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint32_t b = (uint32_t)&ptrs[0];
    uint32_t s1 = 0, s2 = 0, s3 = 0, s4 = 0, w[4] = { 0 };
    for (int pass = 0; pass < 4; pass++) { flush(); s1 += c2x1(b); flush(); s2 += c64x2(b); flush(); s3 += c1024x2(b); }
    for (int pass = 0; pass < 8; pass++) { flush(); s4 += c1024x512(b); }
    for (int pass = 0; pass < 16; pass++) { w[0] += w0(b); w[1] += w1(b); w[2] += w2(b); w[3] += w3(b); }
    ExitCriticalSection();
    printf("COLD, size store at word 0:\n");
    printf("  2x1        %2u/16\n  64x2       %2u/16\n  1024x2     %2u/16\n  1024x512   %2u/8\n", (unsigned)s1, (unsigned)s2, (unsigned)s3, (unsigned)s4);
    printf("WARM, 2x1, the second of two:\n");
    for (int i = 0; i < 4; i++) printf("  word %d     %2u/16\n", i, (unsigned)w[i]);
    printf("\nDone.\n");
    for (;;) {}
}
