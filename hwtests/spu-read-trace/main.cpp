/*
 * spu/spu-read-trace - every read of an SPU register timestamped: does a read ever wait for the SPU's own clock?
 *
 * WHY (2026-10-09). hwtests/spu-busy on the SCPH-5501 and 7501: the CPU polls SPUSTAT for bit 11 to fall (a loop of an
 * SPU read and a Timer 2 read, ~40 cycles), writes SPUCNT mode 1 and polls until the mode shows - and the consoles read
 * 696-699 cycles on every FIFO length, both runs alike to the cycle, where a poll that meets the edge at a random phase
 * of its own ~40-cycle loop would spread ~40 (this core: 662, 668 or 734). spu-read-grid on the 7501 found no grid in an
 * SPU read's cost - 18.94 cycles, the next 16 instructions free in its shadow, this core 18.98 - but it averages 256
 * reads. A read that WAITS once a sample, for a window in which bit 11 also changes, would lock every poll to the
 * sample clock and leave the averages almost unmoved (one read in ~38 delayed). This looks at each read.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock; SPUCNT C000h with 1F801DACh = 0004h (bit 11 toggles every
 * 256 samples). A cached loop: lhu <register>; lhu Timer 2; [k nops]; one word (value << 16 | timer) stored to a buffer
 * - 8,192 iterations for SPUSTAT (longer than one bit-11 half period, so an edge is in it), 4,096 for the rest:
 *   SPUSTAT k = 0, 12, 24;  SPUCNT k = 0;  voice 0's volume (1F801C00h) k = 0;  I_STAT k = 0, 12 (the control: no SPU).
 *
 * READ IT LIKE THIS. Per trace: the read-to-read intervals as value x count (the loop's own period, and whatever DRAM
 * refresh and the write buffer add to it, which the I_STAT control shows); then the LONG ones (more than 2 over the
 * commonest) - how many, their spacing in cycles, and each one's phase in the sample (mod 768) counted from the first
 * bit-11 change the trace saw (SPUSTAT) or from the first long one (the rest). A once-a-sample wait shows as long
 * intervals 768 apart at one phase, in the SPU traces and not in I_STAT's; the edge's phase against them says whether
 * bit 11 changes inside it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-read-trace --frames=1500 --quiet=1200`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

#define NMAX 8192
static uint32_t buf[NMAX];
static uint32_t tt[NMAX];

#define N1 "nop\n"
#define N4 N1 N1 N1 N1
#define N12 N4 N4 N4
#define N24 N12 N12

#define TRACE(READ, NOPS)                                       \
    ".set push\n"                                               \
    ".set noreorder\n"                                          \
    ".set noat\n"                                               \
    "lui   $t4, 0x1f80\n"                                       \
    "1:\n"                                                      \
    READ                                                        \
    "lhu   $t3, 0x1120($t4)\n"                                  \
    NOPS                                                        \
    "sll   $t1, $t1, 16\n"                                      \
    "or    $t3, $t3, $t1\n"                                     \
    "sw    $t3, 0($t6)\n"                                       \
    "addiu $t2, $t2, -1\n"                                      \
    "bnez  $t2, 1b\n"                                           \
    "addiu $t6, $t6, 4\n"                                       \
    ".set pop\n"

#define RUN(READ, NOPS, n)                                                                   \
    do {                                                                                     \
        register uint32_t* p asm("t6") = buf;                                                \
        register uint32_t c asm("t2") = (n);                                                 \
        asm volatile(TRACE(READ, NOPS) : "+r"(p), "+r"(c) : : "t1", "t3", "t4", "memory");   \
    } while (0)

#define RSTAT "lhu   $t1, 0x1dae($t4)\n"
#define RCNT  "lhu   $t1, 0x1daa($t4)\n"
#define RVOL  "lhu   $t1, 0x1c00($t4)\n"
#define RIST  "lhu   $t1, 0x1070($t4)\n"

static void analyse(const char* name, int k, int n, bool spustat) {
    // unwrap the 16-bit timer
    uint32_t t = 0; uint16_t last = (uint16_t)buf[0];
    for (int i = 0; i < n; i++) { const uint16_t v = (uint16_t)buf[i]; t += (uint16_t)(v - last); last = v; tt[i] = t; }
    // the intervals' histogram (values up to 255; longer ones counted apart)
    static uint16_t hist[256];
    for (int i = 0; i < 256; i++) hist[i] = 0;
    int over = 0;
    for (int i = 1; i < n; i++) { const uint32_t d = tt[i] - tt[i - 1]; if (d < 256) hist[d]++; else over++; }
    int mode = 0;
    for (int i = 1; i < 256; i++) if (hist[i] > hist[mode]) mode = i;
    printf("%s k=%d (%d reads over %u cycles): intervals", name, k, n, (unsigned)tt[n - 1]);
    for (int i = 0; i < 256; i++) if (hist[i]) printf(" %dx%d", i, hist[i]);
    if (over) printf(" >255x%d", over);
    printf("\n");
    // the first bit-11 change (SPUSTAT), the time origin for phases
    int edge = -1;
    if (spustat) for (int i = 1; i < n; i++) if (((buf[i] ^ buf[i - 1]) >> 16) & 0x800) { edge = i; break; }
    // the long intervals
    int nl = 0, first = -1, prev = -1;
    static uint32_t gaps[16]; int ng = 0;
    static uint16_t ph[24]; int np = 0;
    for (int i = 1; i < n; i++) {
        const uint32_t d = tt[i] - tt[i - 1];
        if ((int)d <= mode + 2) continue;
        nl++;
        if (first < 0) first = i;
        if (prev >= 0 && ng < 16) gaps[ng++] = tt[i] - tt[prev];
        prev = i;
    }
    const uint32_t org = edge >= 0 ? tt[edge] : (first >= 0 ? tt[first] : 0);
    for (int i = 1; i < n && np < 24; i++) {
        const uint32_t d = tt[i] - tt[i - 1];
        if ((int)d <= mode + 2) continue;
        ph[np++] = (uint16_t)(((tt[i] - org) % 768 + 768) % 768);
    }
    printf("  long (> %d): %d", mode + 2, nl);
    if (ng) { printf("; spacing"); for (int i = 0; i < ng; i++) printf(" %u", (unsigned)gaps[i]); }
    printf("\n");
    if (np) {
        printf("  phase mod 768 from %s:", edge >= 0 ? "the bit-11 change" : "the first long one");
        for (int i = 0; i < np; i++) printf(" %u", ph[i]);
        printf("\n");
    }
    if (edge >= 0) {
        printf("  bit 11 %s at read %d: read %d at %u, read %d at %u (cycles from the trace's start), intervals around it",
               ((buf[edge] >> 16) & 0x800) ? "rose" : "fell", edge, edge - 1, (unsigned)tt[edge - 1], edge, (unsigned)tt[edge]);
        for (int i = edge - 3; i <= edge + 3; i++) if (i >= 1 && i < n) printf(" %u", (unsigned)(tt[i] - tt[i - 1]));
        printf("\n");
    }
}

int main() {
    printf("\nspu/spu-read-trace\n");
    printf("Every read of an SPU register timestamped (Timer 2, the system clock): the read-to-read intervals, and the long\n");
    printf("ones' spacing and phase in the sample (768 cycles). No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(0x1F801DACu, 0x0004);
    write16(0x1F801DAAu, 0xC000);

    RUN(RSTAT, "", 8192);  analyse("SPUSTAT", 0, 8192, true);
    RUN(RSTAT, N12, 8192); analyse("SPUSTAT", 12, 8192, true);
    RUN(RSTAT, N24, 8192); analyse("SPUSTAT", 24, 8192, true);
    RUN(RCNT, "", 4096);   analyse("SPUCNT", 0, 4096, false);
    RUN(RVOL, "", 4096);   analyse("VOL0", 0, 4096, false);
    RUN(RIST, "", 4096);   analyse("I_STAT", 0, 4096, false);
    RUN(RIST, N12, 4096);  analyse("I_STAT", 12, 4096, false);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
