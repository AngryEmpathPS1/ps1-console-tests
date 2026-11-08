/*
 * sio1/sio1-sample64 - sio1-sample with the receiver at MUL64 (BAUD 5: 320-cycle bits, 5-cycle ticks), the sender at MUL1 320 + 2m
 * (2026-11-07). v0.377.304 measured the MUL16 receiver (126 + U[0, 18) at BAUD 18: the next tick, then F/2 - 1 ticks) and gave MUL64
 * the same mechanism unmeasured - here s = 155 + U[0, 5) if it holds, 160 if MUL64 samples the middle. Everything below is
 * sio1-sample's, with 288 read as 320 and the receiver's MODE / BAUD as said.
 *
 * (sio1-sample:) WHERE IN A BIT THE SIO1 RECEIVER SAMPLES: a sender's bit stepped 2 cycles at a time against the receiver's
 * 288, and the first sample that falls into a neighbouring bit read off each byte (2026-11-07).
 *
 * WHY. hwtests/sio1-rates on the real pair (console-logs/2026-11-07-link-rates, -swap): a sender 5.6% slow (BAUD 19, 304-cycle
 * bits) into a receiver at 18 (288) puts the receiver's stop-bit sample exactly where the sender's stop bit begins (9.5 x 288 =
 * 9 x 304 = 2,736) - and the consoles read the bit BEFORE it (a bad stop on every byte whose bit 7 is 0), where this emulator,
 * which samples each bit at its exact middle from the start edge (SerialPort.rxFinish), reads the stop bit. 5.6% fast the same
 * from the other side. So the console samples earlier than the middle - by how much, and with what spread (the receiver's
 * 16x tick finding the start edge would scatter it over 18 cycles), is this test.
 *
 * WHAT IT DOES. As sio1-rates ('#' and a role from the PC; the runner's port, MODE 4Eh, BAUD 18, between rows; B sends 'R' and
 * the row's number, switches, takes bytes for 8 ms; A switches on the 'R' + 3 ms). A sends 8 bytes of alternating bits (55h,
 * or 15h / 35h... for 5-7 bits - 1, 0, 1, 0 from bit 0) at MUL1 with a reload of 288 + 2m (MUL1 drops the reload's bit 0:
 * psx-spx "AND NOT 1"), each after TX idle + 1,000 cycles - so every frame starts from an idle line and is decoded alone - and
 * B takes them at MUL16, BAUD 18, the same length. A sample k bits after the start edge at s + 288k falls in the sender's bit
 * k - 1 once s + 288k < kL (a slow sender) and in bit k + 1 once s + 288k >= (k + 1)L (a fast one): with alternating bits each
 * such bit reads inverted, so the received byte gives the first bit that slipped, and the bad-stop flag the stop's. Rows: 8, 7,
 * 6 and 5 data bits, m = -20 .. 20 (0 a control at the receiver's own rate at MUL1). B prints every row; tools/linkrun.js
 * --test=sio1-sample (--swap the other way); tools/linkemu.js --test=sio1-sample --tty-seconds=10 for this emulator's pair.
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
#define CTRL_ON  0x0027
#define CTRL_ACK 0x0010
#define M_MAX 20
#define PER_LEN (2 * M_MAX + 1)
#define N_ROWS (4 * PER_LEN)
#define N_BYTES 8

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
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

/* row r: data bits 8 - r / PER_LEN, m = r % PER_LEN - M_MAX */
static int bitsOf(int r) { return 8 - r / PER_LEN; }
static int mOf(int r) { return r % PER_LEN - M_MAX; }
static uint16_t lenBits(int n) { return (uint16_t)((n - 5) << 2); }
static uint8_t patOf(int n) { return (uint8_t)(0x55 & ((1 << n) - 1)); }

static uint8_t got[N_ROWS][N_BYTES], flg[N_ROWS][N_BYTES], nGot[N_ROWS];
static int missedR;

int main() {
    printf("\nsio1/sio1-sample64\nWhere in a bit the SIO1 receiver samples, at MUL64.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    setPort(DEF_MODE, DEF_BAUD); write16(SIO_CTRL, CTRL_ON);
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
    idle(MS(1500));
    flush();

    for (int r = 0; r < N_ROWS; r++) {
        const int n = bitsOf(r), m = mOf(r);
        if (role == 'A') {
            bool ok = false; const uint32_t t = now(); int state = 0;
            while (now() - t < MS(2000)) {
                if (!rxReady()) continue;
                const uint8_t c = read8(SIO_DATA);
                if (state == 0) { if (c == 'R') state = 1; }
                else { if (c == (uint8_t)r) { ok = true; break; } state = (c == 'R') ? 1 : 0; }
            }
            if (!ok) missedR++;
            idle(MS(3));
            setPort((uint16_t)(0x41 | lenBits(n)), (uint16_t)(320 + 2 * m));     /* MUL1, n data bits, no parity, 1 stop */
            idle(MS(1));
            for (int i = 0; i < N_BYTES; i++) { txIdle(MS(50)); idle(1000); write8(SIO_DATA, patOf(n)); }
            txIdle(MS(50)); idle(MS(1));
            setPort(DEF_MODE, DEF_BAUD);
            flush();
        } else {
            flush();
            send('R'); send((uint8_t)r); txIdle(MS(50));
            setPort((uint16_t)(0x43 | lenBits(n)), 5);                          /* MUL64 BAUD 5: 320 a bit, n data bits, 1 stop */
            flush();
            int k = 0; const uint32_t t = now();
            while (now() - t < MS(8)) {
                const uint16_t st = read16(SIO_STAT);
                if (!(st & 2)) continue;
                const uint8_t c = read8(SIO_DATA);
                if (k < N_BYTES) { got[r][k] = c; flg[r][k] = (uint8_t)(st & 0x38); k++; }
                write16(SIO_CTRL, CTRL_ON | CTRL_ACK);
            }
            nGot[r] = (uint8_t)k;
            setPort(DEF_MODE, DEF_BAUD);
            idle(MS(1));
        }
    }
    flush();
    waitBang();
    flush();
    ExitCriticalSection();
    if (role == 'A') printf("role A: %d rows sent; 'R' missed %d times\n", N_ROWS, missedR);
    else {
        printf("role B: each row: data bits, the sender's bit (320 + 2m cycles at MUL1), the receiver MUL64 BAUD 5 (320), then each byte taken as XX and its flags (P, O, F bad stop, . none); sent 55h masked to the length\n");
        for (int r = 0; r < N_ROWS; r++) {
            printf("n%d m%+3d L%3d: %d:", bitsOf(r), mOf(r), 320 + 2 * mOf(r), nGot[r]);
            for (int i = 0; i < nGot[r]; i++) {
                const uint8_t f = flg[r][i];
                printf(" %02X%s%s%s%s", got[r][i], (f & 8) ? "P" : "", (f & 0x10) ? "O" : "", (f & 0x20) ? "F" : "", f ? "" : ".");
            }
            printf("\n");
        }
    }
    printf("go %s, role %c\nsio1/sio1-sample64: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
