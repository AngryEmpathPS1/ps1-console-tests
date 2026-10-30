/*
 * sio/sio1-mismatch - SIO1's receiver at a rate the transmitter is not running (2026-10-19)
 *
 * WHY. v0.377.136 rebuilt this emulator's SIO1 receiver on the Intel 8251A's rules (psx-spx: the port "is apparently
 * based/inspired on the Intel 8251A"; Intel 205222-002, "Asynchronous Mode (Receive)"): a falling edge starts a START bit,
 * checked again at its centre at 16X / 64X, and after a framing error the receiver carries on and takes the next falling
 * edge. Until then it computed each frame whole when the sender began it, at the receiver's rate then. The two agree
 * whenever both ends run one rate - every console log here does - and differ only when they do not. Nothing on a console
 * has shown which is the port's. This asks it, through a loopback plug: the port's own transmitter is its receiver's
 * sender, and BAUD is written after a byte goes into TX_DATA, so the frame on the wire and the receiver's rate can differ.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Each row: the port reset to 8N1 MUL16 at BAUD A, the
 * receiver emptied, a byte (or a burst) written to TX_DATA, then BAUD B written `at` cycles after the write - before the
 * start bit begins (the start is a bit after the write: sio1-loopback), or inside the frame - then a long wait, and every
 * byte in the receiver read out with SR's error bits (3 parity, 4 overrun, 5 bad stop bit) as they stand.
 *   R2  55h at 576 cycles a bit, BAUD then 288: the receiver TWICE as fast (8251A: 66h, then E6h from the falling edge at
 *       the slow frame's bit 6 - the old model 66h alone)
 *   R4  02h at 288, BAUD then 1,152: the receiver four times SLOWER (8251A: the start strobed at its centre lands on a high
 *       bit: a false start, and the next falling edge is taken)
 *   RM  55h at 576, BAUD 288 written INSIDE the frame (after its start bit): does the receiver keep the rate it began with?
 *   RB  a burst of four bytes at 64, BAUD then 1,152: F1's race start (a fast packet into a slow receiver)
 *   R1  the same write with BAUD rewritten to A (the control: the byte as sent)
 *
 * THE CABLE SWAP, as sio1-loopback (whose framework this is): the TV turns BLUE (put the loopback plug in), YELLOW
 * (measuring), GREEN (put the serial cable back); the results print 20 s after the plug is out. Nothing is pressed.
 * Run here with `--loopback-auto`. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js sio1-mismatch --loopback-auto --frames=12000 --quiet=6000`.
 */
#include <common.h>
#include <timer.h>
#include <psxapi.h>
#include <stdio.h>
#include <stdarg.h>

#define SIO_DATA 0x1f801050
#define SIO_STAT 0x1f801054
#define SIO_MODE 0x1f801058
#define SIO_CTRL 0x1f80105a
#define SIO_BAUD 0x1f80105e

#define TXEN  0x0001
#define DTR   0x0002
#define RXEN  0x0004
#define ACK   0x0010
#define RTS   0x0020
#define RESET 0x0040
#define ON (TXEN | DTR | RXEN | RTS)

static char report[8192];
static int reportLen = 0;
static void out(const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(tmp, fmt, ap);
    va_end(ap);
    for (const char* q = tmp; *q && reportLen < (int)sizeof(report) - 1; q++) report[reportLen++] = *q;
    report[reportLen] = 0;
}
/* A long string through the kernel's printf in 60-character pieces (sio1-loopback: its %s cuts one off at ~88). */
static void say(const char* text) {
    char piece[61];
    for (int i = 0; text[i]; i += 60) {
        int n = 0;
        while (n < 60 && text[i + n]) { piece[n] = text[i + n]; n++; }
        piece[n] = 0;
        printf("%s", piece);
        if (n < 60) break;
    }
}
static void port(uint16_t mode, uint16_t baud, uint16_t ctrl) {
    write16(SIO_CTRL, RESET);
    write16(SIO_MODE, mode);
    write16(SIO_BAUD, baud);
    write16(SIO_CTRL, ctrl);
}
static inline uint16_t sr() { return read16(SIO_STAT); }
static void waitMs(uint32_t ms) {
    const uint16_t m = read16(0x1f801124);
    write16(0x1f801124, 0);
    uint16_t last = read16(0x1f801120);
    uint32_t acc = 0;
    const uint32_t want = ms * 33869u;
    while (acc < want) { const uint16_t t = read16(0x1f801120); acc += (uint16_t)(t - last); last = t; }
    write16(0x1f801124, m);
}

#define GP0 0x1f801810
#define GP1 0x1f801814
#define BLUE   0xff0000
#define YELLOW 0x00ffff
#define GREEN  0x00ff00
static void gp0cmd(uint32_t v) { while (!(read32(GP1) & 0x04000000)) {} write32(GP0, v); }
static void gp0(uint32_t v) { write32(GP0, v); }
static void screen(uint32_t bgr) {
    write32(GP1, 0x08000001);
    write32(GP1, 0x05000000);
    write32(GP1, 0x06000000 | 0x260 | (0xc60 << 12));
    write32(GP1, 0x07000000 | 0x10 | (0x100 << 10));
    gp0cmd(0x02000000 | bgr);
    gp0(0x00000000);
    gp0((240 << 16) | 320);
    write32(GP1, 0x03000000);
}
static bool linesFollow() {
    write16(SIO_CTRL, 0);
    waitMs(2);
    const uint16_t off = sr();
    write16(SIO_CTRL, DTR | RTS);
    waitMs(2);
    const uint16_t on = sr();
    return ((off >> 7) & 3) != ((on >> 7) & 3);
}
static bool echoes() {
    port(0x4e, 0x0012, TXEN | RXEN | DTR | RTS);
    while (sr() & 2) read8(SIO_DATA);
    static const uint8_t pat[3] = {0xa5, 0x5a, 0xc3};
    for (int i = 0; i < 3; i++) {
        write8(SIO_DATA, pat[i]);
        bool got = false;
        for (int k = 0; k < 20000 && !got; k++) if (sr() & 2) { if (read8(SIO_DATA) != pat[i]) return false; got = true; }
        if (!got) return false;
    }
    return true;
}
static int plugIn() {
    int run = 0;
    for (int t = 0; t < 2400; t++) {
        if (linesFollow()) { if (++run >= 10) return 1; } else run = 0;
        if (t >= 400 && t % 20 == 0 && echoes() && echoes()) return 2;
        waitMs(50);
    }
    return 0;
}
static void plugOut(int how) {
    int run = 0;
    for (int t = 0; t < 2400 && run < (how == 1 ? 20 : 5); t++) {
        if (how == 1 ? !linesFollow() : !echoes()) run++; else run = 0;
        waitMs(50);
    }
}

/* Timer 2 at the system clock, unwrapped (read at least every 65,536 cycles). */
static uint32_t clk2; static uint16_t last2;
static void t2start() { write16(0x1f801124, 0); last2 = read16(0x1f801120); clk2 = 0; }
static uint32_t t2now() { const uint16_t t = read16(0x1f801120); clk2 += (uint16_t)(t - last2); last2 = t; return clk2; }

/*
 * One row: 8N1 MUL16 at BAUD a, the bytes written to TX_DATA (each when SR.0 frees it), BAUD b written `at` cycles after
 * the first write, `wait` cycles more, then every byte the receiver holds and SR's error bits.
 */
static void row(const char* name, uint16_t a, uint16_t b, uint32_t at, const uint8_t* bytes, int n, uint32_t wait) {
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1f801124);
    port(0x4e, a, ON);
    waitMs(1);
    while (sr() & 2) read8(SIO_DATA);
    write16(SIO_CTRL, ON | ACK);                       // the error bits cleared
    t2start();
    write8(SIO_DATA, bytes[0]);
    const uint32_t w = t2now();
    int sent = 1;
    bool rebaud = false;
    uint32_t bAt = 0;
    for (;;) {
        const uint32_t t = t2now();
        if (!rebaud && t - w >= at) { write16(SIO_BAUD, b); bAt = t2now() - w; rebaud = true; }
        if (sent < n && (sr() & 1)) { write8(SIO_DATA, bytes[sent]); sent++; }
        if (rebaud && sent == n && t - w >= at + wait) break;
    }
    const uint16_t s = sr();
    uint8_t got[16]; int k = 0;
    while ((sr() & 2) && k < 16) got[k++] = read8(SIO_DATA);
    write16(0x1f801124, m2);
    ExitCriticalSection();
    out("  %-4s BAUD %04x -> %04x at +%-5u:", name, a, b, (unsigned)bAt);
    for (int i = 0; i < k; i++) out(" %02x", got[i]);
    if (!k) out(" (nothing)");
    out("   PE %d OE %d FE %d\n", (s >> 3) & 1, (s >> 4) & 1, (s >> 5) & 1);
}

static void measure(int how) {
    out("THE PLUG: %s\n", how == 1 ? "seen by its lines" : how == 2 ? "seen by its echo only" : "not seen in two minutes");
    out("8N1 MUL16; each row: the bytes written, BAUD rewritten +N cycles after the first write; the receiver's bytes, SR.3/4/5\n");
    static const uint8_t b55[1] = {0x55}, b02[1] = {0x02}, burst[4] = {0x49, 0x0b, 0xff, 0xc4};
    for (int rep = 0; rep < 2; rep++) {
        row("R1", 0x0024, 0x0024, 100, b55, 1, 60000);        // control: the byte as sent (576 a bit)
        row("R2", 0x0024, 0x0012, 100, b55, 1, 60000);        // the receiver twice as fast, before the start bit
        row("R2b", 0x0024, 0x0012, 400, b55, 1, 60000);       // ...and later, still before the start bit (576 after the write)
        row("RM", 0x0024, 0x0012, 1500, b55, 1, 60000);       // inside the frame: after the start bit, 2 bits in
        row("R4", 0x0012, 0x0048, 50, b02, 1, 120000);        // the receiver four times slower (288 -> 1,152)
        row("R4b", 0x0012, 0x0048, 50, b55, 1, 120000);
        row("RB", 0x0002, 0x0048, 20, burst, 4, 120000);      // four bytes at 32 a bit into a receiver at 1,152
    }
}

int main() {
    printf("\nsio/sio1-mismatch\n");
    printf("SIO1's receiver at another rate than its transmitter, through a loopback plug (4-1 TXD-RXD, 6-3 DTR-DSR,\n");
    printf("8-5 RTS-CTS): BAUD rewritten after a byte is written. No assertions.\n\n");
    const uint16_t uMode = read16(SIO_MODE), uBaud = read16(SIO_BAUD), uCtrl = read16(SIO_CTRL);
    say(">>> NO BUTTONS IN THIS TEST - WATCH THE TV. When it turns BLUE: take the serial cable out of the console and put the loopback plug in. YELLOW: it is measuring (a few seconds). GREEN: take the loopback plug out and put the serial cable back - no hurry: the results come 20 s after the plug is out.");
    printf("\n");
    waitMs(3000);
    screen(BLUE);
    const int how = plugIn();
    waitMs(1500);
    screen(YELLOW);
    measure(how);
    waitMs(3000);
    screen(GREEN);
    if (how) plugOut(how);
    waitMs(20000);
    write16(SIO_MODE, uMode);
    write16(SIO_BAUD, uBaud);
    write16(SIO_CTRL, uCtrl);
    say(report);
    printf("\nDone.\n");
    for (;;) {}
}
