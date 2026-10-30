/*
 * gte/gte-hold - does a GTE register write reach the command that is running
 * when the NEXT instruction has to wait for that command?
 *
 * Two silicon facts that one timing model cannot both reproduce (STATUS M262):
 * - pcsx-redux nugget's gte-latency (RESULTS.md, SCPH-5501): `RTPS; ctc2
 *   canary, H; 60 nops` CHANGES the result - RTPS H boundary N = 1, so at
 *   N = 0 the command reads the H written after it.
 * - Viva Soccer (SLUS-00953) shipped with its sprite scaler running
 *       ctc2 v0,H; nop; nop; RTPS; ctc2 v1,H; mfc2 t4,MAC0
 *   (80024E6C..80) - H set to 100h for one RTPS and restored at once. Read
 *   the restore and every player draws twice as wide; the game does not look
 *   like that. So there the restore is NOT read.
 * The difference is what follows the write: nops, or an MFC2 that waits for
 * the command. M262 models the R3000's whole-pipeline stall holding the CTC2
 * one stage ahead of the waiting MFC2 until the command completes. This
 * measures it, and how far back the hold reaches:
 *
 *     ctc2 A,H; nop; nop; RTPS; ctc2 B,H; K nops; <waiter>; 60 nops; read MAC0
 *
 * for K = 0..4 and each waiter psx-spx names ("GTE Clock Cycles": reading a
 * cop2 register or issuing a command) - MFC2 MAC0 (Viva Soccer's), CFC2 FLAG,
 * SWC2 MAC0, and a command (SQR, which leaves MAC0 alone) - plus NONE, the
 * nugget probe's own N = 0. MAC0 is then graded against the two references,
 * the same code with A,A and with B,B:
 *   A = the command did not see the write (held, or H already read)
 *   B = it did
 *   ? = neither (MAC0 also printed)
 * Cached (KSEG0) and uncached (the same code through KSEG1), interrupts off,
 * each probe run once to warm the I-cache and then again (UNSTABLE if the two
 * passes differ). Scene: RT identity, TR (0,0,1000h), V0 (100h,80h,0), DQA
 * 100h, DQB 0, so SZ3 = 1000h and MAC0 = 100000h for H = 100h, 200000h for
 * H = 200h.
 *
 * There are no assertions - compare against your emulator.
 *
 * WHAT A CONSOLE DECIDES: the MFC2 row's K = 0 cell is Viva Soccer, and M262
 * predicts A; NONE is the nugget probe, B on SCPH-5501. The first K that
 * reads B in each waiter's row is the reach of the hold, which M262 sets at
 * one instruction (only K = 0 held) because that is all the game shows. The
 * other waiters say whether CFC2, SWC2 and a command hold as the MFC2 does.
 *
 * ON THIS CORE AT v0.262 (2026-09-28, `node hwtests/run.js gte-hold`):
 *     C MFC2 MAC0  K=0..4: A B B B B      (MAC0 100000h held, 200000h read)
 *     C CFC2 FLAG  K=0..4: A B B B B
 *     C SWC2 MAC0  K=0..4: A B B B B
 *     C SQR (cmd)  K=0..4: A B B B B
 *     C NONE:              B              (the nugget probe's N = 0)
 *     U every row:         A              (uncached, the CTC2 is fetched after
 *                                          H's latch whatever follows it)
 * Cached, only K = 0 holds: the model's one-instruction reach. The cold-cache
 * warm-up pass reads some K >= 1 cells A (a line fill delays the CTC2 past the
 * latch), which is why each row reports UNSTABLE and its warm pass apart.
 * The core before M262 (v0.261) reads B in every cached cell: THE TEST TELLS
 * THE TWO MODELS APART, and a console decides between them.
 */
#include <common.h>
#include <psxapi.h>

#define MAX_K 4

typedef uint32_t (*probe_fn)(uint32_t, uint32_t, uint32_t*);
#define KSEG1(p) ((probe_fn)(((uint32_t)(p)) | 0xA0000000u))

/* H = a, two nops (Sony's cop2 delay slots), RTPS as Viva Soccer encodes it
 * (4A180001h), H = b on the next instruction, K nops, the waiter, a settled
 * MAC0. .set noreorder keeps the assembler out of the sequence. */
#define MK(NAME, K, WAITER)                                                 \
    extern "C" uint32_t __attribute__((noinline))                           \
    NAME##_##K(uint32_t a, uint32_t b, uint32_t* scratch) {                 \
        uint32_t out, tmp;                                                  \
        __asm__ volatile(                                                   \
            ".set push\n\t.set noreorder\n\t"                               \
            "ctc2 %2, $26\n\t"                                              \
            "nop\n\tnop\n\t"                                                \
            ".word 0x4A180001\n\t"                                          \
            "ctc2 %3, $26\n\t"                                              \
            ".rept " #K "\n\tnop\n\t.endr\n\t"                              \
            WAITER                                                          \
            ".rept 60\n\tnop\n\t.endr\n\t"                                  \
            "mfc2 %0, $24\n\t"                                              \
            "nop\n\tnop\n\t"                                                \
            ".set pop\n\t"                                                  \
            : "=&r"(out), "=&r"(tmp) : "r"(a), "r"(b), "r"(scratch) : "memory"); \
        return out;                                                         \
    }
#define MK_ALL(NAME, WAITER) MK(NAME, 0, WAITER) MK(NAME, 1, WAITER) MK(NAME, 2, WAITER) \
    MK(NAME, 3, WAITER) MK(NAME, 4, WAITER)
#define TABLE(NAME) { NAME##_0, NAME##_1, NAME##_2, NAME##_3, NAME##_4 }

MK_ALL(w_mfc2, "mfc2 %1, $24\n\t")        /* Viva Soccer's */
MK_ALL(w_cfc2, "cfc2 %1, $31\n\t")
MK_ALL(w_swc2, "swc2 $24, 0(%4)\n\t")
MK_ALL(w_cmd,  ".word 0x4AA00428\n\t")    /* SQR sf=1 lm=1: MAC1-3, IR1-3 */
MK(w_none, 0, "")                         /* nugget gte-latency's N = 0 */

struct Waiter { const char* name; probe_fn fn[MAX_K + 1]; int ks; };
static const Waiter WAITERS[] = {
    { "MFC2 MAC0", TABLE(w_mfc2), MAX_K + 1 },
    { "CFC2 FLAG", TABLE(w_cfc2), MAX_K + 1 },
    { "SWC2 MAC0", TABLE(w_swc2), MAX_K + 1 },
    { "SQR (cmd)", TABLE(w_cmd), MAX_K + 1 },
    { "NONE", { w_none_0 }, 1 },
};

#define CTC2(R, V) __asm__ volatile("ctc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))
#define MTC2(R, V) __asm__ volatile("mtc2 %0, $" #R "\n\tnop\n\tnop" : : "r"((uint32_t)(V)))

static void gteEnable() {
    uint32_t sr;
    __asm__ volatile("mfc0 %0, $12" : "=r"(sr));
    sr |= 0x40000000;
    __asm__ volatile("mtc0 %0, $12; nop; nop" : : "r"(sr));
}

static void scene() {
    CTC2(0, 0x1000); CTC2(1, 0); CTC2(2, 0x1000); CTC2(3, 0); CTC2(4, 0x1000);   /* RT identity */
    CTC2(5, 0); CTC2(6, 0); CTC2(7, 0x1000);                                     /* TR */
    CTC2(24, 160 << 16); CTC2(25, 120 << 16);                                    /* OFX, OFY */
    CTC2(27, 0x100); CTC2(28, 0);                                                /* DQA, DQB */
    MTC2(0, (0x80u << 16) | 0x100u); MTC2(1, 0);                                 /* V0 */
}

static const uint32_t HA = 0x100, HB = 0x200;
static uint32_t scratchWord;

static uint32_t run(probe_fn f, uint32_t a, uint32_t b) { scene(); return f(a, b, &scratchWord); }

static void sweep(const Waiter& w, int uncached) {
    uint32_t warm[MAX_K + 1], got[MAX_K + 1], refA[MAX_K + 1], refB[MAX_K + 1];
    for (int k = 0; k < w.ks; k++) {
        probe_fn f = uncached ? KSEG1(w.fn[k]) : w.fn[k];
        warm[k] = run(f, HA, HB);
        refA[k] = run(f, HA, HA);
        refB[k] = run(f, HB, HB);
        got[k] = run(f, HA, HB);
    }
    int unstable = 0;
    printf("%c %-10s K=0..%d:", uncached ? 'U' : 'C', w.name, w.ks - 1);
    for (int k = 0; k < w.ks; k++) {
        char c = got[k] == refA[k] ? 'A' : got[k] == refB[k] ? 'B' : '?';
        if (got[k] != warm[k]) unstable = 1;
        printf(" %c", c);
    }
    printf("   MAC0=[");
    for (int k = 0; k < w.ks; k++) printf("%s%x", k ? " " : "", got[k]);
    printf("] A=%x B=%x%s\n", refA[0], refB[0], unstable ? " UNSTABLE" : "");
    if (unstable) {   /* the first, cold-cache pass read differently: print it too */
        printf("  warm pass:");
        for (int k = 0; k < w.ks; k++) printf(" %c", warm[k] == refA[k] ? 'A' : warm[k] == refB[k] ? 'B' : '?');
        printf("\n");
    }
}

int main() {
    initVideo(320, 240);
    printf("\ngte/gte-hold\n");
    printf("ctc2 A,H; nop; nop; RTPS; ctc2 B,H; K nops; <waiter>: did RTPS see B?\n");
    printf("A = no (held, or H read already), B = yes, ? = neither. Viva Soccer is MFC2 K=0;\n");
    printf("NONE is nugget gte-latency's N=0 (B on SCPH-5501). There are no assertions.\n\n");
    EnterCriticalSection();
    gteEnable();
    for (int u = 0; u < 2; u++) {
        for (unsigned i = 0; i < sizeof(WAITERS) / sizeof(WAITERS[0]); i++) sweep(WAITERS[i], u);
        printf("\n");
    }
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) VSync(0);
}
