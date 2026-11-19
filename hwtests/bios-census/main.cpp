/*
 * system/bios-census - every A-, B- and C-table BIOS function that is safe to call, timed on the system clock.
 *
 * WHY (2026-11-12: time every BIOS function on silicon). PS1-DOS's microbench caught the BIOS FlushCache
 * (A(44h)) at 37,612 cycles a call on the second SCPH-5501 and the PSone, against this core's 36,178 (+1,434, every
 * call; STATUS "The BIOS FlushCache runs ~4% longer on the consoles than here"). Everything else it timed agreed.
 * Every game calls the kernel, so this sweeps the whole class of bug on purpose instead of by accident.
 *
 * WHAT IT DOES. Interrupts off (I_MASK 0 and SR.IEc clear, both put back at the end). Each row calls one function
 * through its vector (A0h/B0h/C0h with t1 = the number, or a syscall with a0 = the number) from one stub in cached RAM,
 * with Timer 2 read just before the jalr and just after it returns (bc_call below): fixed arguments, set up again
 * before each call by the row's prep outside the window, 16 calls, min and max. Timer 2 runs on the system clock, or
 * on the system clock / 8 for a row whose first call reads 60,000 cycles or more (such rows print "x8"). Nothing
 * prints inside a window; each row's line is printed after its 16 calls. Where cost scales (memcpy, memset, strlen,
 * bzero, ...) a row per size.
 *   null        the stub alone: jalr to `jr ra; nop` in cached RAM - every other row's overhead
 *   X rows      not BIOS functions: FlushCache's own body (the BIOS's instructions word for word, from bfc01940h to its
 *               return) assembled here and run from RAM through KSEG1 - uncached, as the BIOS runs it from ROM - and
 *               the same with its 1,280 stores (cache isolated) made nops. Against A(44h) they say whether the
 *               console's extra time is the ROM's fetch or the isolated stores.
 * The header prints each function's address from the vector tables (00000200h A, 00000874h B, 00000674h C), so the
 * log says which ones run from ROM (BFC0xxxxh, uncached) and which from the kernel's copy in RAM.
 *
 * SKIPPED, with the reason (psx-spx "BIOS Function Summary", reference/docs/psx-spx.txt 40809-41135):
 *   the file functions A(00h-09h)/B(32h-3Bh), B(40h-46h) (devices: tty prints, cdrom/card need the drive or card),
 *   A(06h)/B(38h) exit, A(3Ah) SystemErrorExit, A(40h), A(4Fh)/A(50h)/A(52h)/A(53h) SystemError (never return),
 *   A(0Bh) atof and A(32h) strtod (COP1, absent: they hang), A(14h) RestoreState (longjmp: does not return here),
 *   A(3Bh-3Fh)/B(3Ch-3Fh) the TTY and printf (the runner hooks putchar/puts; they would print inside the window),
 *   A(41h-43h), A(51h) the EXE loaders, A(A0h) WarmBoot (reboot), A(45h) init_a0_b0_c0_vectors and C(1Ch) AdjustA0Table
 *   (rewrite the tables the runner's putchar hooks live in), A(46h)/A(47h)/A(4Ah)/A(4Bh)/A(4Ch)/A(48h) the GPU's
 *   transfers, DMA and GP1 (change VRAM or the display), A(54h-56h)/A(71h-72h)/A(78h-96h)/A(9Eh)/A(A2h-A6h) the CD,
 *   A(55h)/A(70h)/A(97h)/A(A7h-AFh)/B(4Ah-50h)/B(5Ch)/B(5Dh) the card, B(12h-16h)/B(5Bh) the pad, A(98h)/A(99h)/C(12h)
 *   /C(13h)/C(15h-19h)/B(47h-49h)/B(59h) the TTY devices (add, print or remove devices), A(9Ch) SetConf and
 *   C(00h)/C(01h)/C(07h-09h)/C(0Ch) (re-initialise the kernel), A(9Fh) SetMemSize (memory control), B(02h)/B(04h)-
 *   B(06h)/C(0Ah)/C(0Dh) (timers and I_MASK, under the runner's lock), B(10h) ChangeThread and C(03h) SYS(03h)
 *   (switch threads), B(11h)/B(24h-31h) jump to 0, B(17h) ReturnFromException, B(18h)/B(19h) the exit-from-exception
 *   hook (Unirom's), C(06h) ExceptionHandler, the SystemError and N/A slots.
 *
 * READ IT LIKE THIS. "<row>: min M max X" in system-clock cycles, the stub's overhead included (subtract null).
 * tools/biostime.js runs the same exe on this emulator with a count of what each window fetched and accessed.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT (*(volatile uint32_t *)0x1F801120)
#define T2_MODE  (*(volatile uint32_t *)0x1F801124)
#define I_MASK   (*(volatile uint32_t *)0x1F801074)

struct Call { uint32_t vec, fn, a0, a1, a2, a3, a4, v0, dt; };

extern "C" {
uint32_t bc_call(Call *c);
void bc_null(void);
void bc_sys(void);
void bc_fc_body(void);
void bc_fc_nostore(void);
uint32_t bc_getsr(void);
void bc_setsr(uint32_t);
}

__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    /* bc_call(Call *): a0..a3 from the struct, a fifth at 16(sp), t1 = the number, Timer 2 around the jalr. */
    ".balign 16\n.global bc_call\n"
    "bc_call:\n"
    "  addiu $sp, $sp, -48\n"
    "  sw    $ra, 44($sp)\n  sw    $s0, 40($sp)\n  sw    $s1, 36($sp)\n  sw    $s2, 32($sp)\n"
    "  move  $s2, $a0\n"
    "  lw    $t0, 24($s2)\n"
    "  lw    $a1, 12($s2)\n  lw    $a2, 16($s2)\n  lw    $a3, 20($s2)\n"
    "  sw    $t0, 16($sp)\n"
    "  lw    $t2, 0($s2)\n  lw    $t1, 4($s2)\n  lw    $a0, 8($s2)\n"
    "  lui   $s1, 0x1F80\n"
    "  lhu   $s0, 0x1120($s1)\n"
    "  jalr  $t2\n"
    "  nop\n"
    "  lhu   $t0, 0x1120($s1)\n"
    "  sw    $v0, 28($s2)\n"
    "  subu  $t0, $t0, $s0\n"
    "  andi  $t0, $t0, 0xFFFF\n"
    "  sw    $t0, 32($s2)\n"
    "  move  $v0, $t0\n"
    "  lw    $ra, 44($sp)\n  lw    $s0, 40($sp)\n  lw    $s1, 36($sp)\n  lw    $s2, 32($sp)\n"
    "  jr    $ra\n"
    "  addiu $sp, $sp, 48\n"
    ".balign 16\n.global bc_null\n"
    "bc_null:\n  jr $ra\n  nop\n"
    /* a syscall with a0 = its number (t1 unused) */
    ".balign 16\n.global bc_sys\n"
    "bc_sys:\n  syscall 0\n  jr $ra\n  nop\n"
    ".global bc_getsr\nbc_getsr:\n  mfc0 $v0, $12\n  jr $ra\n  nop\n"
    ".global bc_setsr\nbc_setsr:\n  mtc0 $a0, $12\n  nop\n  jr $ra\n  nop\n"
    /* FlushCache's body as the SCPH-5501's and SCPH-101's ROMs hold it at bfc01940h-bfc01a5ch (their A(44h) at
     * bfc01920h first jumps to its own KSEG1 address, bfc0193ch; tools/disasm.js), here behind its own mfc0 so it
     * returns to its caller. Called through its KSEG1 address: uncached RAM, as the ROM's is uncached. */
    ".balign 16\n.global bc_fc_body\n"
    "bc_fc_body:\n"
    "  mfc0  $t0, $12\n  nop\n"
    "  addiu $t1, $zero, 0x804\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n  lui $t4, 0x1\n  mtc0 $t4, $12\n  nop\n  nop\n"
    "  addiu $t2, $zero, 0\n  addiu $t3, $zero, 0xf80\n"
    "1:\n"
    "  sw $zero, 0x00($t2)\n  sw $zero, 0x10($t2)\n  sw $zero, 0x20($t2)\n  sw $zero, 0x30($t2)\n"
    "  sw $zero, 0x40($t2)\n  sw $zero, 0x50($t2)\n  sw $zero, 0x60($t2)\n  sw $zero, 0x70($t2)\n"
    "  bne $t2, $t3, 1b\n  addi $t2, $t2, 0x80\n"
    "  mtc0 $zero, $12\n  nop\n"
    "  addiu $t1, $zero, 0x800\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n  mtc0 $t4, $12\n  nop\n  nop\n"
    "  addiu $t2, $zero, 0\n  addiu $t3, $zero, 0xf80\n"
    "2:\n"
    "  .irp o, 0x00,0x04,0x08,0x0c,0x10,0x14,0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34,0x38,0x3c,0x40,0x44,0x48,0x4c,0x50,0x54,0x58,0x5c,0x60,0x64,0x68,0x6c,0x70,0x74,0x78,0x7c\n"
    "  sw $zero, \\o($t2)\n"
    "  .endr\n"
    "  bne $t2, $t3, 2b\n  addi $t2, $t2, 0x80\n"
    "  mtc0 $zero, $12\n  nop\n"
    "  lui $t1, 0x1\n  ori $t1, $t1, 0xe988\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n"
    "  mtc0 $t0, $12\n  nop\n"
    "  jr $ra\n  nop\n"
    /* the same, every store in the two loops a nop (the cache still isolated, CACHECTL still written) */
    ".balign 16\n.global bc_fc_nostore\n"
    "bc_fc_nostore:\n"
    "  mfc0  $t0, $12\n  nop\n"
    "  addiu $t1, $zero, 0x804\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n  lui $t4, 0x1\n  mtc0 $t4, $12\n  nop\n  nop\n"
    "  addiu $t2, $zero, 0\n  addiu $t3, $zero, 0xf80\n"
    "3:\n"
    "  .rept 8\n  nop\n  .endr\n"
    "  bne $t2, $t3, 3b\n  addi $t2, $t2, 0x80\n"
    "  mtc0 $zero, $12\n  nop\n"
    "  addiu $t1, $zero, 0x800\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n  mtc0 $t4, $12\n  nop\n  nop\n"
    "  addiu $t2, $zero, 0\n  addiu $t3, $zero, 0xf80\n"
    "4:\n"
    "  .rept 32\n  nop\n  .endr\n"
    "  bne $t2, $t3, 4b\n  addi $t2, $t2, 0x80\n"
    "  mtc0 $zero, $12\n  nop\n"
    "  lui $t1, 0x1\n  ori $t1, $t1, 0xe988\n  lui $at, 0xfffe\n  sw $t1, 0x130($at)\n"
    "  mtc0 $t0, $12\n  nop\n"
    "  jr $ra\n  nop\n"
    ".set pop\n");

#define KSEG1(f) ((uint32_t)(f) | 0xA0000000u)
enum { A = 0xA0, B = 0xB0, C = 0xC0, SYS = 1, LOCAL = 2 };

/* ---- the rows' data ---- */
static char s_a[2100], s_b[2100], s_c[2100];
static uint8_t m_a[4200], m_b[4200];
static int32_t ints[64], ints2[64];
static uint32_t outw[4];
static uint8_t heap[0x10000] __attribute__((aligned(16)));
static uint8_t tstack[1024] __attribute__((aligned(8)));
static uint32_t evt = 0xFFFFFFFF;
static uint32_t intrp[4];

static uint32_t call(uint32_t vec, uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    Call c = { vec, fn, a0, a1, a2, a3, 0, 0, 0 };
    if (vec == SYS) { c.vec = (uint32_t)bc_sys; c.a0 = fn; }
    bc_call(&c);
    return c.v0;
}
static void fill(char *s, int n, char ch) { for (int i = 0; i < n; i++) s[i] = ch + (i % 23); s[n] = 0; }
static void same(char *d, const char *s) { int i = 0; for (; s[i]; i++) d[i] = s[i]; d[i] = 0; }
static void put(char *d, const char *s) { same(d, s); }
static int cmp_int(const void *a, const void *b) { return *(const int32_t *)a - *(const int32_t *)b; }

struct Row;
typedef void (*Prep)(Row *);
typedef void (*Post)(Row *, uint32_t);
struct Row { const char *name; uint32_t vec, fn, a0, a1, a2, a3, a4; Prep prep; Post post; };

/* preps: put the arguments' memory back before each call (outside the window) */
#define P(N) static void N(Row *r __attribute__((unused)))
P(p_str16)   { fill(s_a, 16, 'a'); }
P(p_str256)  { fill(s_a, 256, 'a'); }
P(p_str2048) { fill(s_a, 2048, 'a'); }
P(p_cmp16)   { fill(s_a, 16, 'a'); fill(s_b, 16, 'a'); }
P(p_cmp256)  { fill(s_a, 256, 'a'); fill(s_b, 256, 'a'); }
P(p_cat)     { fill(s_a, 16, 'a'); fill(s_b, 16, 'A'); }
P(p_num)     { put(s_a, "12345"); }
P(p_neg)     { put(s_a, "-12345"); }
P(p_hex)     { put(s_a, "1A2B3C"); }
P(p_tok)     { put(s_a, "alpha beta gamma delta"); }
P(p_set)     { put(s_a, "abcdefghijklmnop:qrst"); put(s_b, ":;,"); }
P(p_spn)     { put(s_a, "aaaaaaaaaaaaaaaab"); put(s_b, "a"); }
P(p_stra)    { fill(s_a, 64, 'a'); put(s_b, "xyz"); }
P(p_mem)     { for (int i = 0; i < 4200; i++) { m_a[i] = i; m_b[i] = i; } }
P(p_sort)    { call(A, 0x39, (uint32_t)heap, sizeof heap); for (int i = 0; i < 16; i++) ints[i] = 16 - i; }
P(p_sorted)  { for (int i = 0; i < 64; i++) ints[i] = i * 3; ints2[0] = 96; }
P(p_ls)      { for (int i = 0; i < 64; i++) ints[i] = i * 3; ints2[0] = 93; }
P(p_heap)    { call(A, 0x39, (uint32_t)heap, sizeof heap); }
P(p_heapm)   { p_heap(r); r->a0 = call(A, 0x33, 64); }
P(p_kalloc)  { r->a0 = call(B, 0x00, 16); }
P(p_open)    { r->a0 = call(B, 0x08, 0xF0FF0001, 0x0001, 0x2000, 0); }
P(p_thread)  { r->a0 = call(B, 0x0E, (uint32_t)bc_null, (uint32_t)(tstack + 1016), 0); }
P(p_enq)     { call(C, 0x02, 3, (uint32_t)intrp); }
P(p_srand)   { call(A, 0x30, 1); }
P(p_evt)     { r->a0 = evt; }
P(p_crit)    { call(SYS, 1); }
static void q_kfree(Row *, uint32_t v)  { call(B, 0x01, v); }
static void q_close(Row *, uint32_t v)  { call(B, 0x09, v); }
static void q_cthr(Row *, uint32_t v)   { call(B, 0x0F, v); }
static void q_deq(Row *, uint32_t)      { call(C, 0x03, 3, (uint32_t)intrp); }
static void q_crit(Row *, uint32_t)     { call(SYS, 1); }

#define S(x) ((uint32_t)(x))
static Row rows[] = {
    { "null (the stub alone)",          LOCAL, 0, 0,0,0,0,0, 0, 0 },
    { "A(44h) FlushCache",               A, 0x44, 0,0,0,0,0, 0, 0 },
    { "X FlushCache body, RAM KSEG1",   LOCAL, 1, 0,0,0,0,0, 0, 0 },
    { "X FlushCache body, no stores",   LOCAL, 2, 0,0,0,0,0, 0, 0 },
    { "A(57h) return 0",                 A, 0x57, 0,0,0,0,0, 0, 0 },
    { "A(73h) return 0",                 A, 0x73, 0,0,0,0,0, 0, 0 },
    { "C(0Eh) return 0",                 C, 0x0E, 0,0,0,0,0, 0, 0 },
    { "A(0Ah) todigit('7')",             A, 0x0A, '7',0,0,0,0, 0, 0 },
    { "A(0Eh) abs(-5)",                  A, 0x0E, S(-5),0,0,0,0, 0, 0 },
    { "A(0Fh) labs(-5)",                 A, 0x0F, S(-5),0,0,0,0, 0, 0 },
    { "A(25h) toupper('a')",             A, 0x25, 'a',0,0,0,0, 0, 0 },
    { "A(26h) tolower('A')",             A, 0x26, 'A',0,0,0,0, 0, 0 },
    { "A(10h) atoi(\"12345\")",          A, 0x10, S(s_a),0,0,0,0, p_num, 0 },
    { "A(11h) atol(\"-12345\")",         A, 0x11, S(s_a),0,0,0,0, p_neg, 0 },
    { "A(0Ch) strtoul(\"1A2B3C\",16)",   A, 0x0C, S(s_a),S(outw),16,0,0, p_hex, 0 },
    { "A(0Dh) strtol(\"-12345\",10)",    A, 0x0D, S(s_a),S(outw),10,0,0, p_neg, 0 },
    { "A(12h) atob(\"12345\")",          A, 0x12, S(s_a),S(outw),0,0,0, p_num, 0 },
    { "A(13h) SaveState",                A, 0x13, S(m_b),0,0,0,0, 0, 0 },
    { "A(1Bh) strlen 0",                 A, 0x1B, S(s_c),0,0,0,0, 0, 0 },
    { "A(1Bh) strlen 16",                A, 0x1B, S(s_a),0,0,0,0, p_str16, 0 },
    { "A(1Bh) strlen 256",               A, 0x1B, S(s_a),0,0,0,0, p_str256, 0 },
    { "A(1Bh) strlen 2048",              A, 0x1B, S(s_a),0,0,0,0, p_str2048, 0 },
    { "A(17h) strcmp 16 equal",          A, 0x17, S(s_a),S(s_b),0,0,0, p_cmp16, 0 },
    { "A(17h) strcmp 256 equal",         A, 0x17, S(s_a),S(s_b),0,0,0, p_cmp256, 0 },
    { "A(18h) strncmp 256,200",          A, 0x18, S(s_a),S(s_b),200,0,0, p_cmp256, 0 },
    { "A(19h) strcpy 16",                A, 0x19, S(s_b),S(s_a),0,0,0, p_str16, 0 },
    { "A(19h) strcpy 256",               A, 0x19, S(s_b),S(s_a),0,0,0, p_str256, 0 },
    { "A(1Ah) strncpy 256,256",          A, 0x1A, S(s_b),S(s_a),256,0,0, p_str256, 0 },
    { "A(15h) strcat 16+16",             A, 0x15, S(s_a),S(s_b),0,0,0, p_cat, 0 },
    { "A(16h) strncat 16+16,8",          A, 0x16, S(s_a),S(s_b),8,0,0, p_cat, 0 },
    { "A(1Ch) index 256 absent",         A, 0x1C, S(s_a),'!',0,0,0, p_str256, 0 },
    { "A(1Dh) rindex 256 absent",        A, 0x1D, S(s_a),'!',0,0,0, p_str256, 0 },
    { "A(1Eh) strchr 256 absent",        A, 0x1E, S(s_a),'!',0,0,0, p_str256, 0 },
    { "A(1Fh) strrchr 256 absent",       A, 0x1F, S(s_a),'!',0,0,0, p_str256, 0 },
    { "A(20h) strpbrk 21,3",             A, 0x20, S(s_a),S(s_b),0,0,0, p_set, 0 },
    { "A(21h) strspn 17,1",              A, 0x21, S(s_a),S(s_b),0,0,0, p_spn, 0 },
    { "A(22h) strcspn 21,3",             A, 0x22, S(s_a),S(s_b),0,0,0, p_set, 0 },
    { "A(23h) strtok first",             A, 0x23, S(s_a),S(" "),0,0,0, p_tok, 0 },
    { "A(24h) strstr 64 absent",         A, 0x24, S(s_a),S(s_b),0,0,0, p_stra, 0 },
    { "A(27h) bcopy 16",                 A, 0x27, S(m_a),S(m_b),16,0,0, p_mem, 0 },
    { "A(27h) bcopy 256",                A, 0x27, S(m_a),S(m_b),256,0,0, p_mem, 0 },
    { "A(28h) bzero 16",                 A, 0x28, S(m_b),16,0,0,0, 0, 0 },
    { "A(28h) bzero 256",                A, 0x28, S(m_b),256,0,0,0, 0, 0 },
    { "A(28h) bzero 2048",               A, 0x28, S(m_b),2048,0,0,0, 0, 0 },
    { "A(29h) bcmp 256 equal",           A, 0x29, S(m_a),S(m_b),256,0,0, p_mem, 0 },
    { "A(2Ah) memcpy 0",                 A, 0x2A, S(m_b),S(m_a),0,0,0, 0, 0 },
    { "A(2Ah) memcpy 16",                A, 0x2A, S(m_b),S(m_a),16,0,0, 0, 0 },
    { "A(2Ah) memcpy 256",               A, 0x2A, S(m_b),S(m_a),256,0,0, 0, 0 },
    { "A(2Ah) memcpy 2048",              A, 0x2A, S(m_b),S(m_a),2048,0,0, 0, 0 },
    { "A(2Bh) memset 16",                A, 0x2B, S(m_b),0x55,16,0,0, 0, 0 },
    { "A(2Bh) memset 256",               A, 0x2B, S(m_b),0x55,256,0,0, 0, 0 },
    { "A(2Bh) memset 2048",              A, 0x2B, S(m_b),0x55,2048,0,0, 0, 0 },
    { "A(2Ch) memmove 256 up",           A, 0x2C, S(m_b + 8),S(m_b),256,0,0, p_mem, 0 },
    { "A(2Dh) memcmp 256 equal",         A, 0x2D, S(m_a),S(m_b),256,0,0, p_mem, 0 },
    { "A(2Eh) memchr 256 absent",        A, 0x2E, S(s_a),'!',256,0,0, p_str256, 0 },
    { "A(30h) srand(1)",                 A, 0x30, 1,0,0,0,0, 0, 0 },
    { "A(2Fh) rand",                     A, 0x2F, 0,0,0,0,0, p_srand, 0 },
    { "A(31h) qsort 16 reversed",        A, 0x31, S(ints),16,4,S(cmp_int),0, p_sort, 0 },
    { "A(36h) bsearch 64",               A, 0x36, S(ints2),S(ints),64,4,S(cmp_int), p_sorted, 0 },
    { "A(35h) lsearch 32 found",         A, 0x35, S(ints2),S(ints),32,4,S(cmp_int), p_ls, 0 },
    { "A(39h) InitHeap 64K",             A, 0x39, S(heap),sizeof heap,0,0,0, 0, 0 },
    { "A(33h) malloc 64",                A, 0x33, 64,0,0,0,0, p_heap, 0 },
    { "A(34h) free",                     A, 0x34, 0,0,0,0,0, p_heapm, 0 },
    { "A(37h) calloc 16x4",              A, 0x37, 16,4,0,0,0, p_heap, 0 },
    { "A(38h) realloc 64->128",          A, 0x38, 0,128,0,0,0, p_heapm, 0 },
    { "A(4Dh) GetGPUStatus",             A, 0x4D, 0,0,0,0,0, 0, 0 },
    { "A(4Eh) gpu_sync",                 A, 0x4E, 0,0,0,0,0, 0, 0 },
    { "A(49h) GPU_cw(GP0 00h)",          A, 0x49, 0,0,0,0,0, 0, 0 },
    { "A(9Dh) GetConf",                  A, 0x9D, S(outw),S(outw + 1),S(outw + 2),0,0, 0, 0 },
    { "A(B4h) GetSystemInfo(0)",         A, 0xB4, 0,0,0,0,0, 0, 0 },
    { "A(B4h) GetSystemInfo(2)",         A, 0xB4, 2,0,0,0,0, 0, 0 },
    { "B(00h) alloc_kernel_memory 16",   B, 0x00, 16,0,0,0,0, 0, q_kfree },
    { "B(01h) free_kernel_memory",       B, 0x01, 0,0,0,0,0, p_kalloc, 0 },
    { "B(03h) get_timer(0)",             B, 0x03, 0,0,0,0,0, 0, 0 },
    { "B(07h) DeliverEvent unopened",    B, 0x07, 0xF0FF0002,0x0001,0,0,0, 0, 0 },
    { "B(20h) UnDeliverEvent unopened",  B, 0x20, 0xF0FF0002,0x0001,0,0,0, 0, 0 },
    { "B(08h) OpenEvent",                B, 0x08, 0xF0FF0001,0x0001,0x2000,0,0, 0, q_close },
    { "B(09h) CloseEvent",               B, 0x09, 0,0,0,0,0, p_open, 0 },
    { "B(0Bh) TestEvent",                B, 0x0B, 0,0,0,0,0, p_evt, 0 },
    { "B(0Ch) EnableEvent",              B, 0x0C, 0,0,0,0,0, p_evt, 0 },
    { "B(0Dh) DisableEvent",             B, 0x0D, 0,0,0,0,0, p_evt, 0 },
    { "B(0Eh) OpenThread",               B, 0x0E, S(bc_null),S(tstack + 1016),0,0,0, 0, q_cthr },
    { "B(0Fh) CloseThread",              B, 0x0F, 0,0,0,0,0, p_thread, 0 },
    { "B(51h) Krom2RawAdd(8140h)",       B, 0x51, 0x8140,0,0,0,0, 0, 0 },
    { "B(53h) Krom2Offset(8140h)",       B, 0x53, 0x8140,0,0,0,0, 0, 0 },
    { "B(54h) GetLastError",             B, 0x54, 0,0,0,0,0, 0, 0 },
    { "B(55h) GetLastFileError(0)",      B, 0x55, 0,0,0,0,0, 0, 0 },
    { "B(56h) GetC0Table",               B, 0x56, 0,0,0,0,0, 0, 0 },
    { "B(57h) GetB0Table",               B, 0x57, 0,0,0,0,0, 0, 0 },
    { "B(58h) get_bu_callback_port",     B, 0x58, 0,0,0,0,0, 0, 0 },
    { "C(04h) get_free_EvCB_slot",       C, 0x04, 0,0,0,0,0, 0, 0 },
    { "C(05h) get_free_TCB_slot",        C, 0x05, 0,0,0,0,0, 0, 0 },
    { "C(02h) SysEnqIntRP(3)",           C, 0x02, 3,S(intrp),0,0,0, 0, q_deq },
    { "C(03h) SysDeqIntRP(3)",           C, 0x03, 3,S(intrp),0,0,0, p_enq, 0 },
    { "C(1Dh) get_card_find_mode",       C, 0x1D, 0,0,0,0,0, 0, 0 },
    { "C(1Ah) set_card_find_mode(0)",    C, 0x1A, 0,0,0,0,0, 0, 0 },
    { "SYS(00h) NoFunction",             SYS, 0, 0,0,0,0,0, 0, 0 },
    { "SYS(01h) EnterCriticalSection",   SYS, 1, 0,0,0,0,0, 0, 0 },
    { "SYS(02h) ExitCriticalSection",    SYS, 2, 0,0,0,0,0, p_crit, q_crit },
};
#define NROWS (sizeof rows / sizeof rows[0])

static uint32_t timed(Row *r, int div8) {
    Call c = { r->vec, r->fn, r->a0, r->a1, r->a2, r->a3, r->a4, 0, 0 };
    if (r->vec == SYS) { c.vec = (uint32_t)bc_sys; c.a0 = r->fn; }
    else if (r->vec == LOCAL) c.vec = r->fn == 0 ? (uint32_t)bc_null : r->fn == 1 ? KSEG1(bc_fc_body) : KSEG1(bc_fc_nostore);
    T2_MODE = div8 ? 0x0200 : 0x0000;
    bc_call(&c);
    I_MASK = 0;
    if (r->post) r->post(r, c.v0);
    return c.dt;
}

static uint32_t vecAddr(uint32_t vec, uint32_t fn) {
    if (vec == A) return ((volatile uint32_t *)0x80000200)[fn];
    if (vec == B) return ((volatile uint32_t *)0x80000874)[fn];
    if (vec == C) return ((volatile uint32_t *)0x80000674)[fn];
    return 0;
}

int main() {
    printf("\nsystem/bios-census\n");
    uint32_t sr = bc_getsr(), mask = I_MASK;
    call(SYS, 1);                        /* EnterCriticalSection */
    I_MASK = 0;
    const char *ver = (const char *)0xBFC7FF32;
    printf("BIOS: \"%s\"\n", ver[0] >= 0x20 && ver[0] < 0x7F ? ver : "(none at BFC7FF32h)");
    printf("BIOS delay/size %08X COM_DELAY %08X\n", *(volatile uint32_t *)0x1F801010, *(volatile uint32_t *)0x1F801020);
    evt = call(B, 0x08, 0xF0FF0003, 0x0001, 0x2000, 0);   /* the event TestEvent/EnableEvent/DisableEvent use */
    intrp[0] = 0; intrp[1] = 0; intrp[2] = (uint32_t)bc_null; intrp[3] = 0;
    s_c[0] = 0;
    for (unsigned i = 0; i < NROWS; i++) {
        Row *r = &rows[i];
        if (r->prep) r->prep(r);
        uint32_t probe = timed(r, 1);
        int div8 = probe >= 7500;
        uint32_t mn = 0xFFFFFFFF, mx = 0;
        for (int k = 0; k < 16; k++) {
            if (r->prep) r->prep(r);
            uint32_t t = timed(r, div8) * (div8 ? 8 : 1);
            if (t < mn) mn = t;
            if (t > mx) mx = t;
        }
        printf("%-34s @%08X: min %u max %u%s\n", r->name, vecAddr(r->vec, r->fn), mn, mx, div8 ? " x8" : "");
    }
    call(B, 0x09, evt);
    T2_MODE = 0;
    I_MASK = mask;
    bc_setsr(sr);
    printf("\nDone.\n");
    for (;;) {}
}
