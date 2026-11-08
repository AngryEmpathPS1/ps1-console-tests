/*
 * sio1/sio1-rates - SIO1 BETWEEN TWO REAL CHIPS: one console sends a known 16 bytes at a rate or a format the other does not
 * expect, and the other notes every byte it takes with its error flags (2026-11-07).
 *
 * WHY. Until now this emulator's SIO1 receiver met a frame at another rate or format only on one console with a loopback plug
 * (hwtests/sio1-mismatch: its own transmitter, the same baud generator), and its rules for a foreign frame - the 8251A's, in
 * SerialPort.rxStart / rxCentre / rxFinish - are read, not measured. Two consoles on the bridge Pico's relay are two chips with
 * two generators. And two questions psx-spx leaves open: SIO_MODE bit 5's parity sense ("0=Even, 1=Odd (seems to be
 * vice-versa...?)"; this core takes it vice-versa) - a sender at 8E1 into a receiver at 8N1 puts the parity bit where the
 * receiver samples its stop bit, so the bad-stop flag reads the parity bit itself; and a TX_DATA write while SR.0 is clear
 * ("causes the old value to be overwritten") - which of a burst of writes cross. Also a receiver at 8N2 under back-to-back 8N1
 * frames (is the second stop bit checked, and does the receiver look for the next start bit in time?), 7 / 8 data bits each way,
 * 1.5 stop bits, rates 5.6-17% off each way, and a MUL1 receiver at the same bit rate.
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-*: unstoppable), the runner's port (MODE 4Eh, BAUD 18: 288 cycles a bit); '#' and a
 * role from the PC. For each row of ROWS: B, at the runner's settings, sends 'R' and the row's number, then switches to the
 * row's receive settings and takes bytes for 25 ms - each one's SR bits 3-5 (P parity, O overrun, F bad stop) read before it and
 * acknowledged after; A, on the 'R', waits 3 ms, switches to the row's send settings, sends 0x00 FF 55 AA 0F F0 01 80 7F FE 33
 * CC 81 7E 3C C3 (as TX allows; "burst": 16 writes with no wait; "spaced": each after TX idle + 1,000 cycles), and switches
 * back. '!' from the PC ends it; B prints every row, A what it sent. tools/linkrun.js --test=sio1-rates (--swap: the other
 * direction); tools/linkemu.js --test=sio1-rates --tty-seconds=8 for this emulator's pair.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)
#define SIO_DATA 0x1F801050u
#define SIO_STAT 0x1F801054u
#define SIO_MODE 0x1F801058u
#define SIO_CTRL 0x1F80105Au
#define SIO_BAUD 0x1F80105Eu
#define DEF_MODE 0x004E
#define DEF_BAUD 0x0012
#define CTRL_ON  0x0027      /* TXEN, DTR, RXEN, RTS */
#define CTRL_ACK 0x0010

struct Row { uint16_t amode, abaud, bmode, bbaud; uint8_t how; };   /* how: 0 as TX allows, 1 burst, 2 spaced */
static const Row ROWS[] = {
    { 0x4E, 18, 0x4E, 18, 0 },   /*  0 control: 8N1 both, the same rate */
    { 0x4E, 15, 0x4E, 18, 0 },   /*  1 the sender 16.7% fast */
    { 0x4E, 16, 0x4E, 18, 0 },   /*  2 11.1% fast */
    { 0x4E, 17, 0x4E, 18, 0 },   /*  3 5.6% fast */
    { 0x4E, 19, 0x4E, 18, 0 },   /*  4 5.6% slow */
    { 0x4E, 20, 0x4E, 18, 0 },   /*  5 11.1% slow */
    { 0x4E, 21, 0x4E, 18, 0 },   /*  6 16.7% slow */
    { 0xCE, 18, 0x4E, 18, 0 },   /*  7 8N2 -> 8N1 */
    { 0x4E, 18, 0xCE, 18, 0 },   /*  8 8N1 -> 8N2, back to back */
    { 0x4E, 18, 0xCE, 18, 2 },   /*  9 8N1 -> 8N2, spaced */
    { 0x4E, 18, 0x8E, 18, 0 },   /* 10 8N1 -> 8N1.5 */
    { 0x5E, 18, 0x4E, 18, 0 },   /* 11 8E1 (bit 5 = 0) -> 8N1: the parity bit where the stop bit is sampled */
    { 0x7E, 18, 0x4E, 18, 0 },   /* 12 8O1 (bit 5 = 1) -> 8N1 */
    { 0x5E, 18, 0x5E, 18, 0 },   /* 13 8E1 -> 8E1 */
    { 0x5E, 18, 0x7E, 18, 0 },   /* 14 8E1 -> 8O1 */
    { 0x4E, 18, 0x5E, 18, 0 },   /* 15 8N1 -> 8E1: the stop bit where parity is sampled */
    { 0x4A, 18, 0x4E, 18, 0 },   /* 16 7N1 -> 8N1, back to back */
    { 0x4A, 18, 0x4E, 18, 2 },   /* 17 7N1 -> 8N1, spaced */
    { 0x4E, 18, 0x4A, 18, 0 },   /* 18 8N1 -> 7N1 */
    { 0x4E, 18, 0x4D, 288, 0 },  /* 19 a MUL1 receiver at the same bit rate */
    { 0x4E, 18, 0x4E, 18, 1 },   /* 20 16 TX_DATA writes with no wait */
    { 0x4E, 18, 0x4E, 18, 2 },   /* 21 control, spaced */
};
#define N_ROWS (int)(sizeof ROWS / sizeof ROWS[0])
static const uint8_t PAT[16] = { 0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0, 0x01, 0x80, 0x7F, 0xFE, 0x33, 0xCC, 0x81, 0x7E, 0x3C, 0xC3 };

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* THE END: the PC's '!' once the relay has closed (sio1-transfer's waitBang). */
static void waitBang() {
    uint32_t acc = 0, secs = 0, p = now();
    while (secs < 240) {
        const uint32_t q = now(); acc += q - p; p = q;
        while (acc >= 33868800u) { acc -= 33868800u; secs++; }
        if ((read16(SIO_STAT) & 2) && read8(SIO_DATA) == '!') break;
    }
}
static void idle(uint32_t c) { const uint32_t t = now(); while (now() - t < c) {} }
static bool rxReady() { return (read16(SIO_STAT) & 2) != 0; }
static void flush() { for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA); write16(SIO_CTRL, CTRL_ON | CTRL_ACK); }
static void setPort(uint16_t mode, uint16_t baud) { write16(SIO_MODE, mode); write16(SIO_BAUD, baud); }
static void txIdle(uint32_t lim) { const uint32_t t = now(); while (!(read16(SIO_STAT) & 4) && now() - t < lim) {} }
static void send(uint8_t b) { const uint32_t t = now(); while (!(read16(SIO_STAT) & 1) && now() - t < MS(50)) {} write8(SIO_DATA, b); }

static uint8_t got[N_ROWS][32], flg[N_ROWS][32], nGot[N_ROWS];
static int missedR;

int main() {
    printf("\nsio1/sio1-rates\nSIO1 between two chips: a rate or a format the receiver does not expect.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    setPort(DEF_MODE, DEF_BAUD); write16(SIO_CTRL, CTRL_ON);           /* the runner's settings */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);

    char role = 'B'; bool go = false;
    const uint32_t t0 = now();
    while (now() - t0 < MS(60000)) {
        if (!rxReady()) continue;
        const uint8_t c = read8(SIO_DATA);
        if (!go) { if (c == '#') go = true; continue; }
        if (c == 'A' || c == 'B') role = (char)c;
        break;
    }
    idle(MS(1500));                                                      /* the relay starts */
    flush();

    for (int r = 0; r < N_ROWS; r++) {
        const Row &w = ROWS[r];
        if (role == 'A') {
            /* B's 'R' and the row's number, at the runner's settings */
            bool ok = false; const uint32_t t = now(); int state = 0;
            while (now() - t < MS(2000)) {
                if (!rxReady()) continue;
                const uint8_t c = read8(SIO_DATA);
                if (state == 0) { if (c == 'R') state = 1; }
                else { if (c == (uint8_t)r) { ok = true; break; } state = (c == 'R') ? 1 : 0; }
            }
            if (!ok) missedR++;
            idle(MS(3));
            setPort(w.amode, w.abaud);
            idle(MS(1));
            for (int i = 0; i < 16; i++) {
                if (w.how == 1) write8(SIO_DATA, PAT[i]);
                else if (w.how == 2) { txIdle(MS(50)); idle(1000); write8(SIO_DATA, PAT[i]); }
                else send(PAT[i]);
            }
            txIdle(MS(50)); idle(MS(2));
            setPort(DEF_MODE, DEF_BAUD);
            flush();
        } else {
            flush();
            send('R'); send((uint8_t)r); txIdle(MS(50));
            setPort(w.bmode, w.bbaud);
            flush();
            int n = 0; const uint32_t t = now();
            while (now() - t < MS(25)) {
                const uint16_t st = read16(SIO_STAT);
                if (!(st & 2)) continue;
                const uint8_t c = read8(SIO_DATA);
                if (n < 32) { got[r][n] = c; flg[r][n] = (uint8_t)(st & 0x38); n++; }
                write16(SIO_CTRL, CTRL_ON | CTRL_ACK);
            }
            nGot[r] = (uint8_t)n;
            setPort(DEF_MODE, DEF_BAUD);
            idle(MS(2));
        }
    }
    flush();
    waitBang();                                                          /* the relay ends, then the PC's '!' */
    flush();
    ExitCriticalSection();
    if (role == 'A') printf("role A: %d rows sent; 'R' missed %d times\n", N_ROWS, missedR);
    else {
        printf("role B: each row: the sender's MODE/BAUD -> the receiver's, then every byte taken as XX and its flags (P parity, O overrun, F bad stop, . none)\n");
        for (int r = 0; r < N_ROWS; r++) {
            const Row &w = ROWS[r];
            printf("row %2d  %02X/%u -> %02X/%u%s: %d:", r, w.amode, w.abaud, w.bmode, w.bbaud, w.how == 1 ? " burst" : w.how == 2 ? " spaced" : "", nGot[r]);
            for (int i = 0; i < nGot[r] && i < 32; i++) {
                const uint8_t f = flg[r][i];
                printf(" %02X%s%s%s%s", got[r][i], (f & 8) ? "P" : "", (f & 0x10) ? "O" : "", (f & 0x20) ? "F" : "", f ? "" : ".");
            }
            printf("\n");
        }
    }
    printf("go %s, role %c\nsio1/sio1-rates: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
