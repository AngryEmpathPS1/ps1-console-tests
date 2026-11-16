/*
 * cpu/seq-fixstart2: what makes a cold line cost a cycle more in seq-fixstart? (the stream refit session, 2026-11-16.)
 *
 * WHY. hwtests/seq-fixstart on the SCPH-7502 and the PSone (byte-identical): a Timer 2 read at each word of a cold line C reads
 * cold - warm 5 (C alone) / 10 (behind a cold line P) at EVERY word, this core 4 / 9; the warm rows exact. There C came two words
 * after a call's return, behind a never-taken branch (the priming run's). Here nothing primes by a branch: the whole block runs warm,
 * then C (and P) alone are evicted - a one-line routine at the same I-cache index 4 KB away is called - and the block runs again.
 * The words before C (line W, warm): N four nops; J a jalr to a warm stub whose return lands at W's word 2 (then two nops' worth:
 * the landing and a nop); B a never-taken branch (bne zero, zero) at word 2, its delay slot at 3.
 * READ IT. Rows "W w mode M r R: cold C warm W d D" (four reps), D = C - W; mode H: C alone cold, S: P and C cold; r 0..3 the read at
 * C's word r, r 4 in D (the warm line after C).
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

#define LHU_T1 0x94890020u   /* lhu t1, 20h(a0)   a0 = 1F801100h: Timer 2 */
#define LHU_T2 0x948A0020u   /* lhu t2, 20h(a0) */
static int cLine, pLine;     /* word index of C's and P's first word */
static void gen(int w, int mode, int r) {
    int i = 0;
    /* E */ CODE[i++] = 0; CODE[i++] = LHU_T1; CODE[i++] = 0x3C190000u | (STUB >> 16); CODE[i++] = 0x37390000u | (STUB & 0xFFFF);
    /* W */ if (w == 0) { for (int q = 0; q < 4; q++) CODE[i++] = 0; }
            else if (w == 1) { CODE[i++] = 0x0320C009u; CODE[i++] = 0; CODE[i++] = 0; CODE[i++] = 0; }   /* jalr t8, t9; delay; (return) nop; nop */
            else { CODE[i++] = 0; CODE[i++] = 0; CODE[i++] = 0x14000004u; CODE[i++] = 0; }              /* nop; nop; bne zero, zero (never); delay */
    pLine = -1; if (mode) { pLine = i; for (int q = 0; q < 4; q++) CODE[i++] = 0; }
    cLine = i; for (int q = 0; q < 4; q++) CODE[i++] = (q == r) ? LHU_T2 : 0;
    /* D */ if (r == 4) CODE[i++] = LHU_T2;
            CODE[i++] = 0xACE90000u;                 /* sw t1, 0(a3) */
            CODE[i++] = 0xACEA0008u;                 /* sw t2, 8(a3) */
            CODE[i++] = 0x03E00008u; CODE[i++] = 0;  /* jr ra */
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
    volatile uint32_t *s = (volatile uint32_t *)STUB; s[0] = 0x03000008u; s[1] = 0; s[2] = 0; s[3] = 0;   /* jr t8 */
    /* the evictors: one line each, 4 KB above C and P (the same I-cache index) */
    for (int k = 0; k < 2; k++) { const int at = (k ? pLine : cLine); if (at < 0) continue; volatile uint32_t *e = CODE + at + 1024; e[0] = 0x03E00008u; e[1] = 0; e[2] = 0; e[3] = 0; }
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, volatile uint32_t*, volatile uint32_t*);
typedef void (*Fn)(void);

static unsigned call() {
    out[0] = out[2] = 0;
    ((Blk)BASE)((volatile uint32_t*)0x1F801100u, 0, 0, out);
    return (unsigned)(uint16_t)((uint16_t)out[2] - (uint16_t)out[0]);
}

static void one(int w, int mode, int r) {
    gen(w, mode, r);
    unsigned c[4], wm[4];
    for (int rep = 0; rep < 4; rep++) {
        ip_flush();
        call(); call();                                   /* everything warm */
        waitT2(500);
        ((Fn)(BASE + 4u * (cLine + 1024)))();             /* evict C */
        if (pLine >= 0) ((Fn)(BASE + 4u * (pLine + 1024)))();   /* evict P */
        waitT2(500);
        c[rep] = call();                                  /* C (and P) cold */
        waitT2(500);
        wm[rep] = call();                                 /* all warm */
        waitT2(500);
    }
    printf("W %c mode %c r %d:", "NJB"[w], mode ? 'S' : 'H', r);
    for (int rep = 0; rep < 4; rep++) printf("  cold %u warm %u d %d", c[rep], wm[rep], (int)c[rep] - (int)wm[rep]);
    printf("\n");
}

int main() {
    printf("\ncpu/seq-fixstart2\n");
    printf("A Timer 2 read at word r of a cold line C (r 4: the warm line after it); C alone cold (H) or behind a cold line (S),\n");
    printf("the warm line before it N (nops), J (a call's return lands at its word 2) or B (a never-taken branch at word 2). d = cold - warm.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int w = 0; w < 3; w++) for (int mode = 0; mode <= 1; mode++) for (int r = 0; r <= 4; r++) one(w, mode, r);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/seq-fixstart2: Done.\n");
    for (;;) VSync(0);
}
