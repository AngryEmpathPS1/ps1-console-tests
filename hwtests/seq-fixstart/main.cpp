/*
 * cpu/seq-fixstart: is a cold fill sequence's extra cycle (the fixup, v0.377.80) spent at the sequence's START or at its end? (the
 * stream refit session, 2026-11-16; docs/TOOL-IDEAS.md.)
 *
 * WHY. v0.377.80 measured a lone cold line at 5 cycles over warm by any entry, and N lines back to back 5 per line plus ONE once a
 * sequence (hwtests/seq-miss2, line-fill2): this core pays that cycle at the sequence's end (its first hit that is not the next
 * word, CB_FIXH). The GTE's input race on the 7502 and the PSone reads it as paid at the START (STATUS "The GTE sees a cold
 * sequence's extra cycle at its start": gteincold 24 / 24 and gtelat's own layout 30 / 30 with it, 29 and 25 without). Does the CPU
 * pay it there too? A Timer 2 read INSIDE the sequence tells: cold minus warm at the read is the miss's cost so far.
 * THE CODE. E (a line: the start read, t9 = the stub), W (jalr to a warm stub and back - the return is a hit - then a branch the
 * priming run takes), mode S only: P (a cold line of nops, so C is the sequence's SECOND line), C (the cold line under test: r nops,
 * lhu Timer 2, then nops; r 4 = the read in D, the warm line after C), D (stores, return). Each case: flush the I-cache, a priming
 * call (a1 0: runs E, W, the stub, D - they are warm), the measuring call (a1 1: P and C cold), and the same call again (all warm).
 * READ IT. Rows "mode M r R: cold C warm W d D", D = C - W, four reps. In H, C is the sequence's first miss; in S its second.
 * Under the core's account D at r 0..3 is the same in H and S less the stream's wait; under "paid at the start" H's D reads one
 * more at every r inside C, and at r 4 (after the sequence's end) both accounts agree.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define CODE ((volatile uint32_t *)0x80030000)
#define BASE 0x80030000u
#define STUB 0x80030400u

extern "C" { void ip_flush(void); }
__asm__(".text\n.set push\n.set noreorder\n.global ip_flush\nip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");

static volatile uint32_t out[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

#define LHU_T1 0x94890020u   /* lhu t1, 20h(a0)   a0 = 1F801100h */
#define LHU_T2 0x948A0020u   /* lhu t2, 20h(a0) */
static int gen(int mode, int r) {
    int i = 0;
    /* E */ CODE[i++] = 0; CODE[i++] = LHU_T1; CODE[i++] = 0x3C190000u | (STUB >> 16); CODE[i++] = 0x37390000u | (STUB & 0xFFFF);
    /* W */ CODE[i++] = 0x0320C009u;                 /* jalr t8, t9 */
            CODE[i++] = 0;                           /* delay */
            const int beq = i; CODE[i++] = 0;        /* beq a1, zero, D (filled below) - the return lands here: a hit */
            CODE[i++] = 0;                           /* delay (a hit) */
    if (mode) for (int q = 0; q < 4; q++) CODE[i++] = 0;   /* P */
    /* C */ for (int q = 0; q < 4; q++) CODE[i++] = (q == r) ? LHU_T2 : 0;
    /* D */ const int d = i;
            if (r == 4) CODE[i++] = LHU_T2;
            CODE[i++] = 0xACE90000u;                 /* sw t1, 0(a3) */
            CODE[i++] = 0xACEA0008u;                 /* sw t2, 8(a3) */
            CODE[i++] = 0x03E00008u; CODE[i++] = 0;  /* jr ra */
    CODE[beq] = 0x10A00000u | ((d - beq - 1) & 0xFFFF);
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
    /* the stub, in its own line */
    volatile uint32_t *s = (volatile uint32_t *)STUB; s[0] = 0x03000008u; s[1] = 0; s[2] = 0; s[3] = 0;   /* jr t8 */
    return i;
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, volatile uint32_t*, volatile uint32_t*);

static unsigned call(uint32_t a1) {
    out[0] = out[2] = 0;
    ((Blk)BASE)((volatile uint32_t*)0x1F801100u, a1, 0, out);
    return (unsigned)(uint16_t)((uint16_t)out[2] - (uint16_t)out[0]);
}

static void one(int mode, int r) {
    gen(mode, r);
    unsigned c[4], w[4];
    for (int rep = 0; rep < 4; rep++) {
        ip_flush();
        call(0);                    /* priming: E, W, the stub, D */
        waitT2(500);
        c[rep] = call(1);           /* P, C cold */
        waitT2(500);
        w[rep] = call(1);           /* all warm */
        waitT2(500);
    }
    printf("mode %c r %d:", mode ? 'S' : 'H', r);
    for (int rep = 0; rep < 4; rep++) printf("  cold %u warm %u d %d", c[rep], w[rep], (int)c[rep] - (int)w[rep]);
    printf("\n");
}

int main() {
    printf("\ncpu/seq-fixstart\n");
    printf("A Timer 2 read at word r of a cold line C (r 4: the warm line after it), C entered by a hit after a return (H: the\n");
    printf("sequence's first miss) or behind a cold line (S: its second). d = cold - warm, four reps. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int mode = 0; mode <= 1; mode++) for (int r = 0; r <= 4; r++) one(mode, r);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/seq-fixstart: Done.\n");
    for (;;) VSync(0);
}
