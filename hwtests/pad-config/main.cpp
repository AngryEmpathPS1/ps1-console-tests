/*
 * sio/pad-config - a DualShock's replies to the configuration commands, and
 * what its ANALOG button does, byte for byte.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, the pad rows). This core's DualShock
 * answers 43h (config mode), 44h (set mode and lock), 45h (status), 46h, 47h
 * and 4Ch (constant tables) and 4Dh (the motor map) with DuckStation's and
 * psx-spx's bytes, which agree - but a scanner dump of a real pad disagreed on
 * one row (M251's JogCon: the dump said 04h where Ridge Racer Type 4 needs
 * 0300h), and nobody has put a real DualShock's replies beside them. The
 * ANALOG button (M117): pressed while a game has the pad in config mode it
 * reports status 00h instead of 5Ah; pressed back it leaves a plain digital
 * pad; 44h can lock it out. psx-spx, DuckStation and two games agree; no
 * console log does.
 *
 * WHAT IT DOES. SIO0 driven by hand at 250 kHz, interrupts off, port 1. Every
 * packet is printed as the bytes sent and the bytes answered; a | follows the
 * pad's last byte (the one it does not acknowledge), and EE is no byte at all.
 *   1. 42h, the poll, digital.  2. 45h outside config mode.  3. 43h 01, into
 *   config.  4. 45h, 46h 00, 46h 01, 47h 00, 4Ch 00, 4Ch 01.  5. 44h 01 00
 *   (analog, not locked), 45h.  6. 4Dh 00 01 FF FF FF FF, twice.  7. 43h 00,
 *   out.  8. 42h, analog.
 *   Then three times the program asks for the ANALOG button to be pressed once
 *   and polls for 10 s, printing each packet whose ID or status changes: A out of
 *   config mode; B inside it (43h 01 first), with 45h polled; C with analog
 *   mode locked by 44h 01 03 (out of config again).
 * It needs a DUALSHOCK (SCPH-1200 or later) in port 1 and a person to press
 * ANALOG when asked. An original digital pad answers only the poll.
 *
 * READ IT LIKE THIS. Each line is one packet; compare byte for byte with this
 * emulator's in the header. The ANALOG rows are the order of the replies'
 * first two bytes (ID and status) as the button is pressed.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js pad-config --press-analog
 * --frames=4000 --quiet=2000`; run.js presses ANALOG at each prompt):
 *   42h            FF 41 5A FF FF, the fifth byte unacknowledged
 *   45h outside    FF 41, and no more: the pad stops acknowledging
 *   43h 01         FF 41 5A FF FF (the poll's answer), then every reply F3h
 *   45h            F3 5A 01 02 00 02 01 00 (LED 00h, digital)
 *   46h 00 / 01    F3 5A 00 00 01 02 00 0A / 00 00 01 01 01 14
 *   47h 00         F3 5A 00 00 02 00 01 00
 *   4Ch 00 / 01    F3 5A 00 00 00 04 00 00 / 00 00 00 07 00 00
 *   44h 01 00      F3 5A and zeros; 45h then reads LED 01h
 *   4Dh            FF FF FF FF FF FF the first time, 00 01 FF FF FF FF the
 *                  second (the map it was given)
 *   43h 00         F3 5A and zeros; the poll then 73 5A FF FF 80 80 80 80
 *   A              73h, then 41h: the button put the pad back to digital
 *   B              45h's status 5Ah -> 00h and its LED byte 00h -> 01h
 *   C              73h throughout: 44h's lock held
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* One packet on port 1: every byte's reply; `acked` is how many bytes were acknowledged. */
static int packet(const uint8_t* tx, int n, uint8_t* rx) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    idle(20000);
    write16(JOY_CTRL, 0x0003);                                  /* TX enable, /JOY1 low */
    idle(1500);
    int acked = 0;
    for (int b = 0; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);                              /* acknowledge any old flag */
        write8(JOY_DATA, tx[b]);
        const uint32_t t0 = now();
        bool ack = false;
        while (now() - t0 < 4000) { if (read32(JOY_STAT) & 0x80) ack = true; if (ack && (read32(JOY_STAT) & 2)) break; }
        while (!(read32(JOY_STAT) & 2) && now() - t0 < 6000) {}
        rx[b] = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : 0xEE;
        if (!ack) { for (int k = b + 1; k < n; k++) rx[k] = 0xEE; break; }
        acked++;
    }
    write16(JOY_CTRL, 0x0000);
    return acked;
}

static void show(const char* name, const uint8_t* tx, int n) {
    uint8_t rx[16];
    const int acked = packet(tx, n, rx);
    printf("  %-22s", name);
    for (int b = 0; b < n; b++) printf(" %02x", tx[b]);
    printf("\n  %-22s", "");
    for (int b = 0; b < n; b++) printf("%s%02x", b == acked + 1 ? "|" : " ", rx[b]);
    printf("\n");
}

static uint8_t scratchRx[16];

/* Poll with `tx` for the whole of `cycles`, printing each reply whose ID or status byte differs from the last one's -
 * and 45h's LED byte (rx[5]); NOT the poll's rx[5]: in analog mode that is the right stick, whose jitter filled the
 * first version's 12 lines in half a second on the SCPH-9001 (2026-10-12) and ended the wait before anyone pressed. */
static void watch(const uint8_t* tx, int n, uint32_t cycles) {
    uint8_t last[16], rx[16];
    for (int i = 0; i < 16; i++) last[i] = 0xAA;
    const uint32_t t0 = now();
    int shown = 0;
    const bool led = tx[1] == 0x45;
    while (now() - t0 < cycles) {
        packet(tx, n, rx);
        if (shown < 12 && (rx[1] != last[1] || rx[2] != last[2] || (led && rx[5] != last[5]))) {
            printf("    at %4u ms:", (unsigned)((now() - t0) / 33869));
            for (int b = 0; b < n; b++) printf(" %02x", rx[b]);
            printf("\n");
            for (int i = 0; i < 16; i++) last[i] = rx[i];
            shown++;
        }
        idle(33869 * 16);                                       /* about once a frame */
    }
}

int main() {
    printf("\nsio/pad-config\n");
    printf("A DualShock's replies, byte for byte, port 1. Needs a DualShock and a person to press ANALOG when asked.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    static const uint8_t poll[9]   = { 0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t cfgIn[9]  = { 0x01, 0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t cfgOut[9] = { 0x01, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t st45[9]   = { 0x01, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t c46a[9]   = { 0x01, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t c46b[9]   = { 0x01, 0x46, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t c47[9]    = { 0x01, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t c4ca[9]   = { 0x01, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t c4cb[9]   = { 0x01, 0x4C, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t mode44[9] = { 0x01, 0x44, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t lock44[9] = { 0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t mot4d[9]  = { 0x01, 0x4D, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF };

    printf("PACKETS (the bytes sent, then the bytes answered; | after the pad's last byte):\n");
    show("42h poll", poll, 9);
    show("45h outside config", st45, 9);
    show("43h 01 into config", cfgIn, 9);
    show("45h status", st45, 9);
    show("46h 00", c46a, 9);
    show("46h 01", c46b, 9);
    show("47h 00", c47, 9);
    show("4Ch 00", c4ca, 9);
    show("4Ch 01", c4cb, 9);
    show("44h 01 00 analog", mode44, 9);
    show("45h status", st45, 9);
    show("4Dh motors", mot4d, 9);
    show("4Dh motors again", mot4d, 9);
    show("43h 00 out of config", cfgOut, 9);
    show("42h poll, analog", poll, 9);

    printf("\nA: press ANALOG once now - the ANALOG button on the controller, ONCE, then leave it.\n");
    printf("   (out of config mode; the light may go on or off - either is fine; watching 10 s)\n");
    watch(poll, 9, 33868800u * 10);
    printf("  (10 s up)\n");
    packet(cfgIn, 9, scratchRx);
    printf("B: press ANALOG once now - the ANALOG button on the controller, ONCE, then leave it.\n");
    printf("   (inside config mode, 45h polled; the light may go on or off; watching 10 s)\n");
    watch(st45, 9, 33868800u * 10);
    printf("  (10 s up)\n");
    show("43h 00 out of config", cfgOut, 9);
    packet(cfgIn, 9, scratchRx);
    show("44h 01 03 analog, locked", lock44, 9);
    show("43h 00 out of config", cfgOut, 9);
    printf("C: press ANALOG once now - the ANALOG button on the controller, ONCE, then leave it.\n");
    printf("   (analog mode locked: the light may not change at all - that is a result too; watching 10 s)\n");
    watch(poll, 9, 33868800u * 10);
    printf("  (10 s up)\n");

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nsio/pad-config: Done.\n");
    for (;;) VSync(0);
}
