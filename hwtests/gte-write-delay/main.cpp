/*
 * gte/gte-write-delay - how many instructions after a write does a read of the same GTE register see the new value?
 *
 * WHY (2026-10-30). psx-spx dev (commit ef3e1fc, its GTE page, no console named) says writes to GTE registers (MTC2, CTC2,
 * LWC2) land 2 cycles late: a read of the register sees the new value only with at least 2 instructions between, cached nops,
 * and the old value otherwise. nocash ("Caution - Store Delay") has 2..3 cycles and counts cycles, not opcodes, so an uncached
 * opcode can pay for all of it. This core has the delay for LZCS -> LZCR and IRGB/IR1-3 alone (lzcrStale, irStale;
 * hwtests/gte-irgb's rows) and reads the new value at N = 0 from every other register. Nothing here has measured a plain read
 * of an ordinary register.
 *
 * WHAT IT DOES. gte-irgb's method, a register reading itself, per register and N:
 *
 *     mtc2 poison, W ; 26 nops (settled) ; mtc2 value, W ; N nops ; mfc2 out, R
 *
 * N = 0 to 8, interrupts off, cached (KSEG0) and uncached (the same code through KSEG1). Data registers VXY0, VZ0, RGBC, OTZ,
 * IR0, SXY0, SZ0, RGB0, MAC0, LZCS, and the same with ctc2/cfc2 on R11R12, TRX, L11L12, RFC, OFX, H, ZSF3, FLAG. The poison is
 * not 0, so `o` below really is the old value and not a read that gave up.
 * COLD AND WARM. Each probe runs twice at once: the first call ever (cold: its lines have never been fetched), then the second
 * (warm: all hits). gte-irgb's probes fall wherever the linker put them; each here starts on a cache line, so the second
 * `mtc2` is always the LAST word of its line (word 27) and the cold call has an instruction-cache miss INSIDE the window at
 * every N, the warm one none. If the delay counts cycles a miss hides it and cold reads new at N = 0; if it counts
 * instructions cold and warm agree, as LZCS -> LZCR did on nugget's four consoles.
 * THE IO COLUMN. One slow filler: `lw` from I_STAT in place of the nops, one instruction and several cycles. psx-spx (5e3c8b2)
 * says of LZCS that the gap is two cycles however they are spent, one I/O read being enough; this core counts instructions.
 * `n` there with `o` at N = 1 says cycles.
 *
 * THE CONTROL is LZCS -> LZCR, gte-irgb's, whose answer is known: minN 2 cached, 1 uncached (nugget's test on four consoles,
 * gte-irgb's 31 logs on seven models here). A console or emulator that reads anything else there is not running this test
 * the way it claims.
 *
 * READ IT LIKE THIS. Per row: minN = the first N reading the new value, warm; then one letter per N = 0..8, warm and cold, n new,
 * o old, ? neither (with the reads printed under it); then io, warm/cold. psx-spx: minN 2 on every row cached.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE (hwtests/README.md): run here first. ON THIS CORE AT v0.377.223 (`PSX_MODEL=SCPH-7501 node hwtests/run.js
 * gte-write-delay --raw`): the control reads minN 2 cached and 1 uncached, cold as warm, io o/o cached and n/n uncached: an
 * instruction count, as lzcrStale says. Every other row reads minN 0 in both passes, all n, io n/n: no write delay on an
 * ordinary register here. The same under the runner on the virtual console.
 * TESTED ON A SCRATCH CORE WITH lzcrStale'S RULE ON EVERY REGISTER: all 19 rows read the control's letters, oonnnnnnn cached
 * and onnnnnnnn uncached. So every row's poison and value tell old from new, and a console's delay will show.
 */
#include <common.h>
#include <psxapi.h>

#define MAX_N 8

typedef uint32_t (*probe_fn)(uint32_t, uint32_t);
#define KSEG1(p) ((probe_fn)(((uint32_t)(p)) | 0xA0000000u))

/* One global function per (register, N), so each can also be called through its KSEG1 alias, as gte-irgb. .set noreorder
 * keeps the assembler from adding its own nops around the cop2 hazard, which would make N a fiction. Word 0: the poison.
 * Words 1-26: nops. Word 27: the write, last in its 16-byte line. */
#define MK(NAME, WOP, ROP, W, R, N)                                         \
    extern "C" uint32_t __attribute__((noinline))                           \
    NAME##_##N(uint32_t poison, uint32_t value) {                           \
        uint32_t out;                                                       \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            ".balign 16\n\t"                                                \
            WOP " %1, $" #W "\n\t"                                          \
            ".rept 26\n\tnop\n\t.endr\n\t"                                  \
            WOP " %2, $" #W "\n\t"                                          \
            ".rept " #N "\n\tnop\n\t.endr\n\t"                              \
            ROP " %0, $" #R "\n\t"                                          \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out) : "r"(poison), "r"(value));                        \
        return out;                                                         \
    }
/* The slow filler: lw from I_STAT in place of the N nops. The read has no side effect; the read below sits in its delay
 * slot and does not use what it loaded. */
#define MKIO(NAME, WOP, ROP, W, R)                                          \
    extern "C" uint32_t __attribute__((noinline))                           \
    NAME##_io(uint32_t poison, uint32_t value) {                            \
        uint32_t out, t;                                                    \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            ".balign 16\n\t"                                                \
            WOP " %2, $" #W "\n\t"                                          \
            ".rept 26\n\tnop\n\t.endr\n\t"                                  \
            WOP " %3, $" #W "\n\t"                                          \
            "lw %1, 0(%4)\n\t"                                              \
            ROP " %0, $" #R "\n\t"                                          \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out), "=&r"(t) : "r"(poison), "r"(value), "r"(0x1F801070u)); \
        return out;                                                         \
    }
#define MK_ALL(NAME, WOP, ROP, W, R)                                                       \
    MK(NAME, WOP, ROP, W, R, 0) MK(NAME, WOP, ROP, W, R, 1) MK(NAME, WOP, ROP, W, R, 2)    \
    MK(NAME, WOP, ROP, W, R, 3) MK(NAME, WOP, ROP, W, R, 4) MK(NAME, WOP, ROP, W, R, 5)    \
    MK(NAME, WOP, ROP, W, R, 6) MK(NAME, WOP, ROP, W, R, 7) MK(NAME, WOP, ROP, W, R, 8)    \
    MKIO(NAME, WOP, ROP, W, R)
#define D_ALL(NAME, W, R) MK_ALL(NAME, "mtc2", "mfc2", W, R)
#define C_ALL(NAME, W) MK_ALL(NAME, "ctc2", "cfc2", W, W)
#define TABLE(NAME) { NAME##_0, NAME##_1, NAME##_2, NAME##_3, NAME##_4, NAME##_5, NAME##_6, NAME##_7, NAME##_8 }, NAME##_io

D_ALL(lzcs_lzcr, 30, 31)   /* the control */
D_ALL(vxy0, 0, 0)
D_ALL(vz0, 1, 1)
D_ALL(rgbc, 6, 6)
D_ALL(otz, 7, 7)
D_ALL(ir0, 8, 8)
D_ALL(sxy0, 12, 12)
D_ALL(sz0, 16, 16)
D_ALL(rgb0, 20, 20)
D_ALL(mac0, 24, 24)
D_ALL(lzcs, 30, 30)
C_ALL(r11r12, 0)
C_ALL(trx, 5)
C_ALL(l11l12, 8)
C_ALL(rfc, 21)
C_ALL(ofx, 24)
C_ALL(h, 26)
C_ALL(zsf3, 29)
C_ALL(flag, 31)

struct Reg {
    const char* name;
    probe_fn fn[MAX_N + 1];
    probe_fn io;
    uint32_t poison, value, oldReads, newReads;
};

#define W32 0x21436587u, 0x12345678u, 0x21436587u, 0x12345678u   /* a register that reads back all 32 bits */
#define W16 0x2143u, 0x1234u, 0x2143u, 0x1234u                   /* 16 bits, read back sign- or zero-extended: positive here */

static const Reg REGS[] = {
    { "LZCS->LZCR (control)", TABLE(lzcs_lzcr), 0x00000001u, 0x00010000u, 31, 15 },
    { "VXY0", TABLE(vxy0), W32 },
    { "VZ0", TABLE(vz0), W16 },
    { "RGBC", TABLE(rgbc), W32 },
    { "OTZ", TABLE(otz), W16 },
    { "IR0", TABLE(ir0), W16 },
    { "SXY0", TABLE(sxy0), W32 },
    { "SZ0", TABLE(sz0), W16 },
    { "RGB0", TABLE(rgb0), W32 },
    { "MAC0", TABLE(mac0), W32 },
    { "LZCS", TABLE(lzcs), W32 },
    { "ctc2 R11R12", TABLE(r11r12), W32 },
    { "ctc2 TRX", TABLE(trx), W32 },
    { "ctc2 L11L12", TABLE(l11l12), W32 },
    { "ctc2 RFC", TABLE(rfc), W32 },
    { "ctc2 OFX", TABLE(ofx), W32 },
    { "ctc2 H", TABLE(h), W16 },
    { "ctc2 ZSF3", TABLE(zsf3), W16 },
    /* FLAG holds bits 12-30 and bit 31 is the OR of some of them: bit 12 is not one of those (psx-spx) */
    { "ctc2 FLAG", TABLE(flag), 0, 0x00001000u, 0, 0x00001000u },
};

static void gteEnable() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

static char letter(const Reg& r, uint32_t v) { return v == r.newReads ? 'n' : v == r.oldReads ? 'o' : '?'; }

static void sweep(const Reg& r, int uncached) {
    uint32_t cold[MAX_N + 1], warm[MAX_N + 1], coldIo, warmIo;
    char a[MAX_N + 2], b[MAX_N + 2];
    int minN = -1, coldMin = -1, odd = 0;
    for (int n = 0; n <= MAX_N; n++) {
        const probe_fn f = uncached ? KSEG1(r.fn[n]) : r.fn[n];
        cold[n] = f(r.poison, r.value);
        warm[n] = f(r.poison, r.value);
    }
    const probe_fn g = uncached ? KSEG1(r.io) : r.io;
    coldIo = g(r.poison, r.value);
    warmIo = g(r.poison, r.value);
    for (int n = 0; n <= MAX_N; n++) {
        a[n] = letter(r, warm[n]);
        b[n] = letter(r, cold[n]);
        if (minN < 0 && a[n] == 'n') minN = n;
        if (coldMin < 0 && b[n] == 'n') coldMin = n;
        if (a[n] == '?' || b[n] == '?') odd = 1;
    }
    a[MAX_N + 1] = b[MAX_N + 1] = 0;
    printf("%c %-21s minN=%2d  warm %s  cold %s  io %c/%c", uncached ? 'U' : 'C', r.name, minN, a, b, letter(r, warmIo), letter(r, coldIo));
    if (coldMin != minN) printf("  COLD minN=%d", coldMin);
    printf("\n");
    if (odd) {
        printf("  warm [");
        for (int n = 0; n <= MAX_N; n++) printf("%s%x", n ? " " : "", warm[n]);
        printf("]\n  cold [");
        for (int n = 0; n <= MAX_N; n++) printf("%s%x", n ? " " : "", cold[n]);
        printf("]\n");
    }
}

int main() {
    printf("\ngte/gte-write-delay\n");
    printf("mtc2 poison; 26 nops; mtc2 value; N nops; mfc2 (ctc2/cfc2 on the ctc2 rows). psx-spx: minN 2 cached.\n");
    printf("minN = the first N that reads the new value, warm. Letters: N = 0..%d, n new, o old, ? neither.\n", MAX_N);
    printf("io = one lw from I_STAT in place of the nops, warm/cold. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    gteEnable();
    for (int u = 0; u < 2; u++) {
        for (unsigned i = 0; i < sizeof(REGS) / sizeof(REGS[0]); i++) sweep(REGS[i], u);
        printf("\n");
    }
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) {}
}
