/*
 * sio/pad-ack-slide - how long a device holds /ACK low, to the cycle: a 1-cycle slide before one JOY_STAT read.
 *
 * WHY (docs/INSTRUMENT-AUDIT.md flag #19, 2026-11-12). src/psx.js SIO_ACK_PULSE 96 (a pad's /ACK, and the PSxMemCard's) and
 * POCKET_ACK_PULSE 42 are widths read through polling loops as wide as the effect: ack-per-device's 37-cycle loop ("the
 * consoles' 67-105" - one loop turn of spread) and pocket-cmds' ~27 (26-57). card-fetch2's latch (JOY_STAT bit 9) sees every
 * pulse but cannot time one. CARD_TIMING's 56 / 80 came off a 51-cycle loop.
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz (JOY_BAUD 88h) - pad-ack-hist's set-up. For each device and byte: the
 * byte written, a dither of D = 0..15 cycles (so the loop below meets the rise at every phase of its own turn: on a console the
 * device's rise is at a random phase anyway, on an emulator it is not), a tight loop on JOY_STAT until bit 7 (the /ACK level)
 * is set, then a jump into a nop sled of N cycles and ONE read of JOY_STAT: bit 7 still set, or not. N = 0..191, each N at all
 * sixteen D (and twice more for a pad's two-mode /ACK: 48 tries an N). Devices: port 1's controller (01 42 00 00 00, the bytes
 * /ACKed 1-4), port 1's card (81 52 00 00: the read command's first four bytes, before any sector is fetched; nothing written),
 * port 2's controller and card (a PocketStation in slot 2 answers as a card). A device that never acknowledges is "none".
 *
 * READ IT LIKE THIS. Per device and byte, how many of the tries still read /ACK low at N: "all to N=a" (every try still low
 * through N = a), the counts across the fall, "none from N=b". The width is where the count halves, plus the loop's own lag
 * (half its turn on average) and the read's place - the same code's on an emulator, so the emulator's halving point against a
 * console's is the width's error, cycle for cycle. The turn of the rise loop is printed (64 passes timed).
 *
 * THE RULE: run it here first: `node hwtests/run.js pad-ack-slide`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define T2 0x1F801120
#define NMAX 192
#define DITHER 16

static inline uint16_t t2() { return read16(T2); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }

/* Write the byte, D cycles of dither, wait for /ACK low (bit 7 set; at most ~2,000 turns), N nops, one JOY_STAT read.
 * Returns 1 still low, 0 high again, -1 no /ACK. warm: straight into the sled and out (nothing written), so its lines are in the
 * cache when the real pass runs right after. */
static int __attribute__((noinline, aligned(64))) slide(uint32_t tx, uint32_t d, uint32_t n, uint32_t warm) {
    int r;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        " lui $t9, 0x1f80\n"
        " la $t8, 8f\n sll $t7, %[d], 2\n subu $t8, $t8, $t7\n"      /* dither target */
        " la $t6, 9f\n sll $t7, %[n], 2\n subu $t6, $t6, $t7\n"      /* sled target */
        " li $t5, 2000\n"
        " bnez %[w], 2f\n nop\n"
        " sb %[tx], 0x1040($t9)\n"
        " jr $t8\n nop\n"
        " .rept 16\n nop\n .endr\n"
        "8:\n"
        "1: lw $t4, 0x1044($t9)\n"
        " addiu $t5, $t5, -1\n"
        " andi $t4, $t4, 0x80\n"
        " bnez $t4, 2f\n nop\n"
        " bnez $t5, 1b\n nop\n"
        " b 3f\n li %[r], -1\n"
        "2: jr $t6\n nop\n"
        " .rept 192\n nop\n .endr\n"
        "9: lw $t4, 0x1044($t9)\n nop\n"
        " andi $t4, $t4, 0x80\n"
        " sltu %[r], $zero, $t4\n"
        "3:\n"
        ".set pop\n"
        : [r] "=&r"(r) : [tx] "r"(tx), [d] "r"(d), [n] "r"(n), [w] "r"(warm) : "t4", "t5", "t6", "t7", "t8", "t9", "memory");
    return r;
}

/* The rise loop's own turn: 64 passes of the same five instructions, the bit never set (JOY_STAT read with nothing in flight). */
static uint32_t __attribute__((noinline, aligned(64))) turn64() {
    uint32_t t;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        " lui $t9, 0x1f80\n li $t5, 64\n"
        " lhu $t0, 0x1120($t9)\n nop\n"
        "1: lw $t4, 0x1044($t9)\n"
        " addiu $t5, $t5, -1\n"
        " andi $t4, $t4, 0x00\n"
        " bnez $t4, 2f\n nop\n"
        " bnez $t5, 1b\n nop\n"
        "2: lhu $t1, 0x1120($t9)\n nop\n"
        " subu %[t], $t1, $t0\n andi %[t], %[t], 0xffff\n"
        ".set pop\n"
        : [t] "=&r"(t) :: "t0", "t1", "t4", "t5", "t9", "memory");
    return t;
}

struct Dev { const char* name; int port; uint8_t tx[5]; int nb; };
static const Dev devs[] = {
    { "port 1 controller", 0, { 0x01, 0x42, 0x00, 0x00, 0x00 }, 4 },
    { "port 1 card", 0, { 0x81, 0x52, 0x00, 0x00, 0 }, 4 },
    { "port 2 controller", 1, { 0x01, 0x42, 0x00, 0x00, 0x00 }, 4 },
    { "port 2 card", 1, { 0x81, 0x52, 0x00, 0x00, 0 }, 4 },
};
#define NDEV 4
static uint8_t still[NDEV][4][NMAX];      /* tries still low at N */
static int tries[NDEV][4], noAck[NDEV][4];

/* One exchange up to byte `upto`, the slide on that byte; the bytes before it run as pad-ack-hist runs them. */
static int exchange(const Dev& v, int upto, uint32_t d, uint32_t n) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    const uint16_t sel = (uint16_t)(0x0003 | (v.port << 13));
    write16(JOY_CTRL, sel);
    wait(1500);
    int r = -1;
    for (int b = 0; b <= upto; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, sel | 0x0010);
        if (b == upto) { slide(0, d, n, 1); r = slide(v.tx[b], d, n, 0); break; }
        write8(JOY_DATA, v.tx[b]);
        int k = 0;
        for (; k < 2000 && !(read32(JOY_STAT) & 0x80); k++) { }
        if (k == 2000) break;                       /* no /ACK before the byte measured: the device is not there */
        for (int j = 0; j < 4000 && (read32(JOY_STAT) & 0x80); j++) { }
        wait(400);
    }
    for (int j = 0; j < 4000 && (read32(JOY_STAT) & 0x80); j++) { }
    wait(400);
    write16(JOY_CTRL, 0x0000);
    wait(2000);                                      /* the device lets go before the next select */
    return r;
}

int main() {
    printf("\nsio/pad-ack-slide\n");
    printf("/ACK low (JOY_STAT bit 7) seen by a tight loop, then N nops and ONE read: tries still low at N, per device and byte.\n");
    printf("No assertions - compare against your emulator, and say what was in each port and slot.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    const uint32_t tu = turn64();
    for (int v = 0; v < NDEV; v++)
        for (int b = 0; b < devs[v].nb; b++) {
            /* a probe first: is anything there? */
            if (exchange(devs[v], b, 0, 0) < 0) { noAck[v][b] = 1; continue; }
            const int reps = devs[v].tx[0] == 0x01 ? 3 : 1;
            for (int rep = 0; rep < reps; rep++)
                for (int n = 0; n < NMAX; n++)
                    for (uint32_t d = 0; d < DITHER; d++) {
                        const int r = exchange(devs[v], b, d, (uint32_t)n);
                        if (r < 0) { noAck[v][b]++; continue; }
                        if (n == 0) tries[v][b]++;
                        still[v][b][n] += (uint8_t)r;
                    }
        }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("the rise loop's turn: %u.%02u cycles (64 passes %u)\n\n", (unsigned)(tu / 64), (unsigned)(tu * 100 / 64 % 100), (unsigned)tu);
    for (int v = 0; v < NDEV; v++) {
        printf("%s:\n", devs[v].name);
        for (int b = 0; b < devs[v].nb; b++) {
            printf("  byte %d %02xh: ", b + 1, devs[v].tx[b]);
            if (!tries[v][b]) { printf("none\n"); continue; }
            const int T = tries[v][b];
            int a = -1, z = NMAX;
            for (int n = 0; n < NMAX && still[v][b][n] == T; n++) a = n;
            for (int n = NMAX - 1; n >= 0 && still[v][b][n] == 0; n--) z = n;
            printf("%d tries a N; all low to N=%d,", T, a);
            for (int n = a + 1; n < z && n < NMAX; n++) printf(" %d:%d", n, still[v][b][n]);
            if (z < NMAX) printf(", none from N=%d", z); else printf(", still low at N=%d", NMAX - 1);
            if (noAck[v][b]) printf("  (%d tries no /ACK)", noAck[v][b]);
            printf("\n");
        }
    }
    printf("\nsio/pad-ack-slide: Done.\n");
    for (;;) VSync(0);
}
