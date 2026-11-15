/*
 * gpu/gpu-replay - A GAME'S OWN DRAWING, WINDOW BY WINDOW, TIMED TO THE CYCLE (2026-11-15; the console fuzzer session, for the GPU
 * timing session: "a GP0 version - the missing reach measure").
 *
 * WHY. The GPU laws are fitted on lone shapes (gpu-shape-time*); a game draws runs of them, from its own textures, at its own places.
 * experiments/fuzz-all/gpuseed.js plays a game in the emulator from a saved state and cuts its GP0 stream into windows - a few
 * consecutive draw packets, the drawing state they ran under, the VRAM they read (texture rects, CLUTs) and the area they draw over -
 * and appends them to this program as a table. Each window is replayed here at the game's own VRAM coordinates.
 *
 * WHAT IT DOES. Per window: the game's GP1(08h) display mode, display start, E1-E6; the texture, CLUT and background rects uploaded
 * (GP0 A0h by DMA2, 16-word blocks: gpuseed.js aligns every rect to 32 halfwords). THE TIMED DRAW is the window's last packet, in
 * the game's context: before each probe the background is restored (for windows that blend or test the mask), then - "prev" - the
 * game's previous up to 8 packets and the window's earlier packets drawn by a DMA2 linked list (as a game's DrawOTag sends them),
 * the texture cache as the game left it; or - "cold" - the window's earlier packets, then GP0(01h). Then at each of NJ start points
 * in the vertical blanking (as gpu-shape-time: the VBlank flag, then 20,000 + 271 j cycles) the last packet by the CPU, its last 2-4
 * words stored back to back, D cycles, one GPUSTAT read; D binary-searched for bit 26 and for bit 28 (the GPU timing session's probe
 * and search, gpu-shape-time81, unchanged). The earlier packets are done (the GPU idle) before the last one starts: a CPU store to a
 * full GP0 FIFO is not settled (src/psx.js GPU_FIFO_OPTS.cpuStall: amidog's overflow against PeterLemon's demos), so the timed words
 * never meet one. THE WHOLE WINDOW is timed too, polled: every packet by one DMA2 list from a start point, Timer 2 from the CHCR
 * store to GPUSTAT 26 and 28 both set (a ~45-cycle poll quantum: the GPU timing session's warning - a rank, not a law).
 * Finally one clean run (background, prelude, window, idle) and the drawn area read back through GPUREAD, a checksum a row.
 *
 *   W <id> <label> d26 <min>..<max> d28 <min>..<max> whole <min>..<max>    (whole: the polled window, DMA2 start to idle)
 *   R <id> stat <GPUSTAT after the clean run> rows <n> <FNV-1a of each row's words ...>
 *
 * The same exe runs here and on the consoles (tools/hwrunner.js); experiments/fuzz-all/gpuseedscore.js reads both. No assertions.
 * Nothing prints inside a window.
 * THE RULE: run it here first: `node tools/hwrunner.js <exe> --emu --lock=0 --raw`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

extern "C" { extern char _end[]; }

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u
#define DPCR    0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static void waitDma2() { const uint32_t t0 = now(); while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < 4000000u) {} }

#define PROBE_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n"
#define PROBE_TAIL \
        "beqz %[n], 3f\n" \
        "nop\n" \
        "4:\n" \
        "addiu %[n], %[n], -1\n" \
        "bnez %[n], 4b\n" \
        "nop\n" \
        "3:\n" \
        "jr $t1\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "2:\n" \
        "lw %[st], 0x1814($t0)\n" \
        "nop\n" \
        ".set reorder\n"
static uint32_t probe2(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe3(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe4(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}

/* one window, as gpuseed.js lays it out (all words):
 *   id, nRect, nPre (packets), nWin (packets), state[8] (GP1 08h, E1, E2, E3, E4, E5, E6, GP1 05h), rb xy, rb wh, flags (bit 0:
 *   restore the background before each probe; bit 1: the display off), bgRect (index of the background among the rects, or FFh),
 *   preE[6] (E1-E6 as the prelude's first packet ran: every probe sets them, so a window's own E-words leave no trace),
 *   rects: [xy, wh, data ...] (w a multiple of 32), packets: [len, words ...] - the prelude's, then the window's */
struct Win {
    uint32_t id, nRect, nPre, nWin; const uint32_t* st; uint32_t rbXY, rbWH, flags, bg; const uint32_t* preE;
    const uint32_t* rect[16]; const uint32_t* pre; const uint32_t* win; const uint32_t* end;
};
static const uint32_t* parse(const uint32_t* p, Win& w) {
    w.id = p[0]; w.nRect = p[1] < 16 ? p[1] : 16; w.nPre = p[2]; w.nWin = p[3]; w.st = p + 4; w.rbXY = p[12]; w.rbWH = p[13];
    w.flags = p[14]; w.bg = p[15]; w.preE = p + 16;
    p += 22;
    for (uint32_t i = 0; i < w.nRect; i++) { w.rect[i] = p; const uint32_t wd = p[1] & 0xFFFF, ht = p[1] >> 16; p += 2 + (wd * ht) / 2; }
    w.pre = p; for (uint32_t i = 0; i < w.nPre; i++) p += 1 + p[0];
    w.win = p; for (uint32_t i = 0; i < w.nWin; i++) p += 1 + p[0];
    w.end = p;
    return p;
}

static void dma2Block(const uint32_t* data, uint32_t words) {   /* words a multiple of 16 */
    write32(GP1, 0x04000002u);
    write32(D2_MADR, (uint32_t)data & 0xFFFFFFu);
    write32(D2_BCR, ((words / 16) << 16) | 16u);
    write32(D2_CHCR, 0x01000201u);
    waitDma2();
    write32(GP1, 0x04000000u);
}
static void uploadRect(const uint32_t* r) {
    waitReady();
    write32(GP0, 0xA0000000u); write32(GP0, r[0]); write32(GP0, r[1]);
    dma2Block(r + 2, ((r[1] & 0xFFFF) * (r[1] >> 16)) / 2);
    waitReady();
}

/* the linked lists (the prelude's packets, and the window's but its last) and the row sums: at fixed RAM, not in BSS - the
 * table is appended past the image (gpuseed.js pads 32 KB first) and a large BSS would be zeroed over it; the runner sits at
 * 80130000h and up (hwtests/runner: snapshot, log, text) */
static uint32_t* const listPre = (uint32_t*)0x80100000u;
static uint32_t* const listWin = (uint32_t*)0x80108000u;
static uint32_t* const rowh = (uint32_t*)0x80110000u;
static uint32_t* const listAll = (uint32_t*)0x80112000u;
static uint32_t buildList(uint32_t* out, const uint32_t* pk, uint32_t n) {   /* returns the list's first node, 0 for none */
    uint32_t at = 0, prev = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t len = pk[0];
        if (at + 1 + len > 8192) break;
        if (prev != 0xFFFFFFFFu) out[prev] = (out[prev] & 0xFF000000u) | ((uint32_t)&out[at] & 0xFFFFFFu);
        out[at] = (len << 24) | 0xFFFFFFu;
        for (uint32_t k = 0; k < len; k++) out[at + 1 + k] = pk[1 + k];
        prev = at; at += 1 + len; pk += 1 + len;
    }
    return at ? (uint32_t)&out[0] : 0;
}
static void sendList(uint32_t first) {
    if (!first) return;
    write32(GP1, 0x04000002u);
    write32(D2_MADR, first & 0xFFFFFFu);
    write32(D2_BCR, 0);
    write32(D2_CHCR, 0x01000401u);
}

static void setState(const Win& w) {
    waitReady();
    write32(GP1, 0x08000000u | (w.st[0] & 0xFFFFFFu));
    write32(GP1, 0x05000000u | (w.st[7] & 0xFFFFFFu));
    write32(GP1, (w.flags & 2) ? 0x03000001u : 0x03000000u);
    for (int i = 1; i <= 6; i++) write32(GP0, w.st[i]);
    waitReady();
}

static const uint32_t* lastPk;   /* the window's last packet: [len, words ...] */
static uint32_t firstWin;        /* its list (all but the last) */
static uint32_t firstAll;        /* every packet of the window */
static void env(const uint32_t* e) { waitReady(); for (int i = 0; i < 6; i++) write32(GP0, e[i]); waitReady(); }
static void runList(uint32_t first) { if (!first) return; sendList(first); waitDma2(); write32(GP1, 0x04000000u); waitReady(); }
/* the context before a timed draw: the background, the game's state, its prelude (prev) or not (cold), and for the last-packet
 * probes the window's earlier packets - with the texture cache cleared after them for cold */
static void prepare(const Win& w, bool cold, uint32_t firstPre, bool earlier) {
    waitReady();
    if ((w.flags & 1) && w.bg != 0xFFu) uploadRect(w.rect[w.bg]);
    if (!cold && firstPre) { env(w.preE); runList(firstPre); } else env(w.st + 1);
    if (earlier) runList(firstWin);
    if (cold) { write32(GP0, 0x01000000u); waitReady(); }
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t wt = 20000u + 271u * (uint32_t)j; while (now() - t0 < wt) {} }
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n, len = lastPk[0];
    const uint32_t* p = lastPk + 1;
    if (len <= 4) return len == 2 ? probe2(p, n, k) : len == 3 ? probe3(p, n, k) : probe4(p, n, k);
    for (uint32_t i = 0; i < len - 4; i++) write32(GP0, p[i]);
    return probe4(p + len - 4, n, k);
}
static const uint32_t MAXD = 65000;
static uint32_t search(const Win& w, bool cold, uint32_t firstPre, uint32_t bit, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        prepare(w, cold, firstPre, true);
        sync(j);
        const uint32_t st = probe(mid);
        waitReady();
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static uint32_t whole(const Win& w, bool cold, uint32_t firstPre, int j) {
    prepare(w, cold, firstPre, false);
    sync(j);
    const uint32_t t0 = now();
    sendList(firstAll);
    while ((read32(D2_CHCR) & 0x01000000u) || (read32(GP1) & 0x14000000u) != 0x14000000u) { if (now() - t0 > 4000000u) break; }
    const uint32_t t = now() - t0;
    write32(GP1, 0x04000000u);
    waitReady();
    return t;
}
static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

int main() {
    printf("\ngpu/gpu-replay\n");
    printf("A game's own GP0 windows (experiments/fuzz-all/gpuseed.js), replayed at its VRAM places: bit 26's and bit 28's first D\n");
    printf("after the last word, over start points in the blanking, after the game's previous packets (prev) and after GP0(01h) (cold).\n");
    const uint32_t* tb = (const uint32_t*)(((uint32_t)_end + 15) & ~15u);
    for (int i = 0; i < 4096 && tb[0] != 0x31525047u; i++) tb += 4;
    if (tb[0] != 0x31525047u) { printf("no window table\ngpu/gpu-replay: Done.\n"); for (;;) VSync(0); }
    const uint32_t nW = tb[1], flags = tb[2], NJ = ((flags >> 8) & 15) ? ((flags >> 8) & 15) : 4;
    printf("windows %u, start points %u, labels %s\n\n", (unsigned)nW, (unsigned)NJ, (flags & 1) ? "prev+cold" : "prev");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);   /* DMA2 on */
    const uint32_t* p = tb + 4;
    for (uint32_t wi = 0; wi < nW; wi++) {
        Win w; const uint32_t* next = parse(p, w);
        setState(w);
        for (uint32_t r = 0; r < w.nRect; r++) uploadRect(w.rect[r]);
        const uint32_t firstPre = buildList(listPre, w.pre, w.nPre);
        lastPk = w.win; for (uint32_t i = 0; i + 1 < w.nWin; i++) lastPk += 1 + lastPk[0];
        firstWin = w.nWin > 1 ? buildList(listWin, w.win, w.nWin - 1) : 0;
        firstAll = buildList(listAll, w.win, w.nWin);
        setState(w);
        /* warm the code (and the lists) once */
        prepare(w, false, firstPre, true); sync(0); probe(MAXD); waitReady(); whole(w, false, firstPre, 0);
        uint32_t res[2][6];
        for (int lab = 0; lab < ((flags & 1) ? 2 : 1); lab++) {
            uint32_t a = 0xFFFFFFFFu, b = 0, c = 0xFFFFFFFFu, d = 0, e = 0xFFFFFFFFu, f = 0;
            for (uint32_t j = 0; j < NJ; j++) {
                const uint32_t x = search(w, lab == 1, firstPre, 0x04000000u, (int)j), y = search(w, lab == 1, firstPre, 0x10000000u, (int)j);
                const uint32_t z = whole(w, lab == 1, firstPre, (int)j);
                a = x < a ? x : a; b = x > b ? x : b; c = y < c ? y : c; d = y > d ? y : d; e = z < e ? z : e; f = z > f ? z : f;
            }
            res[lab][0] = a; res[lab][1] = b; res[lab][2] = c; res[lab][3] = d; res[lab][4] = e; res[lab][5] = f;
        }
        /* the clean run (the background as the game had it, the prelude, every packet, idle), then the drawn area read back */
        waitReady();
        if (w.bg != 0xFFu) uploadRect(w.rect[w.bg]);
        if (firstPre) { env(w.preE); runList(firstPre); } else env(w.st + 1);
        runList(firstAll);
        const uint32_t stat = read32(GP1);
        const uint32_t rw = w.rbWH & 0xFFFF, rh = (w.rbWH >> 16) < 512 ? (w.rbWH >> 16) : 512;
        write32(GP0, 0x01000000u);
        write32(GP0, 0xC0000000u); write32(GP0, w.rbXY); write32(GP0, w.rbWH);
        { const uint32_t t0 = now(); while (!(read32(GP1) & 0x08000000u) && now() - t0 < 4000000u) {} }
        for (uint32_t y = 0; y < rh; y++) { uint32_t h = 2166136261u; for (uint32_t x = 0; x < rw / 2; x++) h = fnv(h, read32(GP0)); rowh[y] = h; }
        waitReady();
        for (int lab = 0; lab < ((flags & 1) ? 2 : 1); lab++)
            printf("W %u %s d26 %u..%u d28 %u..%u whole %u..%u\n", (unsigned)w.id, lab ? "cold" : "prev", (unsigned)res[lab][0], (unsigned)res[lab][1],
                   (unsigned)res[lab][2], (unsigned)res[lab][3], (unsigned)res[lab][4], (unsigned)res[lab][5]);
        printf("R %u stat %x rows %u", (unsigned)w.id, (unsigned)stat, (unsigned)rh);
        for (uint32_t y = 0; y < rh; y++) printf(" %x", (unsigned)rowh[y]);
        printf("\n");
        p = next;
    }
    write32(GP1, 0x04000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-replay: Done.\n");
    for (;;) VSync(0);
}
