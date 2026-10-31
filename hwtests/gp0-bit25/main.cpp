/*
 * gpu/gp0-bit25 - with GP1(04h)=1, at how many queued words does GPUSTAT.25 fall?
 *
 * WHY (2026-10-30). psx-spx's dev text: with GP1(04h)=1 bit 25 "reads 0 once about 11 words are queued behind a busy GPU,
 * before the 16 words are used up". nocash's text says the flag clears at FIFO size 09h. This core reads size 09h, which is
 * the 9th or 10th word written. Neither text says how a word written becomes a word queued, and on the GPU they differ:
 * nocash's "FIFO Prefetch" has a busy GPU take one attribute word off the FIFO and hold it, and a quad leave its last vertex
 * in the FIFO until its first half is drawn. "About 11" may be 9 queued read as words written, or a different flag.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, every wait bounded. GP1(04h)=1, then a long command, 200 cycles
 * for it to get going, then GP0(E6h) words, GPUSTAT read after each. Three busy commands: a 256x128 VRAM copy (80h), a
 * 640x480 fill, a 256x256 flat quad; each lasts more than 15,000 cycles, far longer than the words take. Each at two paces:
 * FAST, the word and the read back to back; SETTLED, about 100 cycles between the word and the read, so that anything the
 * GPU takes at once has been taken. Three runs of each. After the last word: cycles until bit 25 and bit 28 read set again,
 * and until bits 26+28 do (the FIFO draining).
 *   24 WORDS   the question: the word at which bit 25 falls.
 *   CONTROLS   the same 24 words to an idle GPU, and 24 GP0(00h) NOPs behind the copy (which psx-spx says take no FIFO
 *              space): bit 25 must stay set through both, or falling says nothing about the FIFO.
 *   48 WORDS   the wrap. FIFO size is a 5-bit count, (wr - rd) AND 1Fh (nocash, "FIFO Overrun"; amidog's FIFO > OVERFLOW
 *              grades it), so the 32nd QUEUED word makes it 0: bit 25 reads set again, and falls a second time as many words
 *              later as it would from an empty FIFO. A word that is overrun overwrites an older E6h, which is harmless here.
 *
 * READ IT LIKE THIS, and this is how words written are told from words queued. On a 48-word row: F the word bit 25 first
 * falls, W the word it rises again (the 32nd queued), F2 the word it falls again.
 *   taken = W - 32   words the GPU had taken off the FIFO (a held attribute word: nocash's "FIFO Prefetch"), or, negative,
 *                    words already queued before the first E6h (a quad's last vertex, until its first half is drawn).
 *   T = F2 - W       the queued count that clears bit 25, with nothing taken.
 *   F - taken        must equal T, or the wrap is not reading what it claims.
 * psx-spx's "about 11" holds if T is 10 to 12; nocash's and this core's rule says 9. The 24-word rows alone tell only words
 * written, and each busy command's offset shows there as a different F; W is what names it. On this core bit 28 rises at W too
 * behind the copy and the fill, not behind a quad (a polygon holds it low until it draws), which is why W is read from bit 25.
 * SETTLED against FAST shows whether the reading depends on the pace. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js gp0-bit25 --raw --frames=4000`. PREDICTED.txt
 * has what this core read before any console ran it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_MASK   0x1F801074u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define GP0W (*(volatile uint32_t*)0x1F801810u)
#define GPUSTAT (*(volatile uint32_t*)0x1F801814u)
#define B25 0x02000000u
#define B26 0x04000000u
#define B28 0x10000000u
#define LIMIT 400000u                   /* cycles: well past every busy command here; every wait stops at it */
#define NONE 0xFFFFFFFFu

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static int waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & (B26 | B28)) != (B26 | B28)) if (now() - t0 > LIMIT) return 0;
    return 1;
}
/* about 2 cycles a turn, from the I-cache */
static inline void spin(uint32_t n) {
    __asm__ volatile(".set noreorder\n1:\nbnez %0, 1b\naddiu %0, %0, -1\n.set reorder\n" : "+r"(n));
}

enum { K_IDLE, K_COPY, K_FILL, K_QUAD };
static const char* const KIND[4] = { "idle", "copy", "fill", "quad" };

static void busy(int kind) {
    if (kind == K_COPY) { write32(GP0, 0x80000000u); write32(GP0, xy(0, 256)); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 128)); }
    else if (kind == K_FILL) { write32(GP0, 0x02102030u); write32(GP0, xy(0, 0)); write32(GP0, xy(640, 480)); }
    else if (kind == K_QUAD) { write32(GP0, 0x28102030u); write32(GP0, xy(0, 0)); write32(GP0, xy(256, 0)); write32(GP0, xy(0, 256)); write32(GP0, xy(256, 256)); }
}

static uint32_t st[48];
static char s25[49], s26[49], s28[49], first25[49], first26[49], first28[49];

static int same(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

struct Row { uint32_t before, tWords, t25, t28, tIdle; int stale, fall, wrap, fall2; };

/* the words, then the times; `gap` turns of spin between each word and its read, 0 for back to back */
static Row trial(int kind, int n, uint32_t gap, uint32_t word) {
    Row r;
    r.stale = !waitIdle();
    write32(GP1, 0x04000001u);          /* direction 1: bit 25 is "FIFO not more than half full" */
    busy(kind);
    spin(100);                          /* 200 cycles: the command is running, its words taken or not */
    r.before = read32(GP1);
    const uint32_t tw = now();
    if (gap) { for (int i = 0; i < n; i++) { GP0W = word; spin(gap); st[i] = GPUSTAT; } }
    else { for (int i = 0; i < n; i++) { GP0W = word; st[i] = GPUSTAT; } }
    const uint32_t t0 = now();
    r.tWords = t0 - tw;
    r.t25 = r.t28 = r.tIdle = NONE;
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if (r.t25 == NONE && (s & B25)) r.t25 = t;
        if (r.t28 == NONE && (s & B28)) r.t28 = t;
        if ((s & (B26 | B28)) == (B26 | B28)) { r.tIdle = t; break; }
        if (t > LIMIT) break;
    }
    write32(GP1, 0x01000000u);          /* an overrun leaves the FIFO's count wrong: clear it */
    write32(GP1, 0x04000000u);
    r.fall = r.wrap = r.fall2 = 0;
    for (int i = 0; i < n; i++) {
        const uint32_t s = st[i];
        s25[i] = (s & B25) ? '1' : '0'; s26[i] = (s & B26) ? '1' : '0'; s28[i] = (s & B28) ? '1' : '0';
        if (!r.fall) { if (!(s & B25)) r.fall = i + 1; }
        else if (!r.wrap) { if (s & B25) r.wrap = i + 1; }
        else if (!r.fall2) { if (!(s & B25)) r.fall2 = i + 1; }
    }
    s25[n] = s26[n] = s28[n] = 0;
    return r;
}

/* Three runs. The patterns of the first; a later run's only if they differ from it. */
static void rowLine(int kind, const char* pace, const char* what, int n, uint32_t gap, uint32_t word) {
    printf("%s, %s, %d words of %s:\n", KIND[kind], pace, n, what);
    for (int run = 1; run <= 3; run++) {
        const Row r = trial(kind, n, gap, word);
        printf(" run %d: before 25/26/28 %d%d%d, words took %u, bit 25 falls at word %d; then bit 25 set %u, bit 28 set %u, idle %u%s\n",
               run, (int)((r.before >> 25) & 1), (int)((r.before >> 26) & 1), (int)((r.before >> 28) & 1), (unsigned)r.tWords, r.fall,
               (unsigned)r.t25, (unsigned)r.t28, (unsigned)r.tIdle, r.stale ? "  *** NOT IDLE BEFORE THE RUN ***" : "");
        if (run == 1 || !same(s25, first25) || !same(s26, first26) || !same(s28, first28))
            printf("  25 %s\n  26 %s\n  28 %s\n", s25, s26, s28);
        if (run == 1) for (int i = 0; i <= n; i++) { first25[i] = s25[i]; first26[i] = s26[i]; first28[i] = s28[i]; }
        if (n == 48) {
            if (r.wrap && r.fall2)
                printf("  F %d, W %d, F2 %d: taken %d, T %d, F-taken %d%s\n", r.fall, r.wrap, r.fall2, r.wrap - 32, r.fall2 - r.wrap,
                       r.fall - (r.wrap - 32), r.fall - (r.wrap - 32) == r.fall2 - r.wrap ? "" : "  *** DISAGREE ***");
            else printf("  F %d, W %d, F2 %d: no wrap seen\n", r.fall, r.wrap, r.fall2);
        }
    }
}

int main() {
    printf("\ngpu/gp0-bit25\n");
    printf("GP1(04h)=1: GPUSTAT.25 read after each GP0(E6h) word written behind a busy GPU. Patterns read left to right, word 1 first.\n");
    printf("psx-spx dev: falls at about 11 words queued. nocash and this core: FIFO size 9. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    if (!waitIdle()) printf("*** the GPU never read idle at the start ***\n");

    printf("CONTROLS: bit 25 must stay 1 through both\n");
    rowLine(K_IDLE, "settled", "E6h", 24, 50, 0xE6000000u);
    rowLine(K_COPY, "settled", "NOP", 24, 50, 0x00000000u);

    printf("\n24 WORDS OF E6h BEHIND A BUSY GPU\n");
    for (int k = K_COPY; k <= K_QUAD; k++) {
        rowLine(k, "fast", "E6h", 24, 0, 0xE6000000u);
        rowLine(k, "settled", "E6h", 24, 50, 0xE6000000u);
    }

    printf("\n48 WORDS: THE 5-BIT COUNT WRAPS AT THE 32ND QUEUED WORD\n");
    for (int k = K_COPY; k <= K_QUAD; k++) rowLine(k, "settled", "E6h", 48, 50, 0xE6000000u);

    waitIdle();
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE6000000u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gp0-bit25: Done.\n");
    for (;;) {}
}
