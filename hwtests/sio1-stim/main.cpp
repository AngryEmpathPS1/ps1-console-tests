/*
 * sio1/sio1-stim - THE CONSOLE'S SIO1 UNDER A PICO'S EXACT STIMULUS: every change of SIO_STAT and every byte received, Timer 2
 * stamped, while the bridge Pico plays a waveform into the console's RxD / CTS / DSR (2026-11-24, the console fuzzer, for
 * AngryEmpath: the dual-channel hardware-debugging roadmap's flagship item).
 *
 * WHY. SIO1 is the line every console test runs over, so it could never be the thing under test: hwtests/sio1-rates, -sample,
 * -cannon used a second console as the sender, as exact as its own SIO1. pico-serial v7.05's STIM mode (tools/sio1stim.js) sends
 * any waveform to 6.7 ns - exact rates, bad stop bits, breaks, gaps, CTS / DSR - and times every edge of both directions; the
 * GameShark 1001's DB-25 carries this test's settings in and its result out. The receiver's sampling point, its FIFO and overrun,
 * framing and parity flags, DSR / CTS status and IRQ timing can now be measured against a known input.
 *
 * WHAT IT DOES. Under hwtests/runner, on a GameShark console with its Pico up (dbio.h). 1. The settings from the PC over the DB-25
 * (16 bytes, tools/sio1run.js queues them): BAUD reload, MODE, CTRL (written as given, so RX enable, DTR / RTS out, IRQ enables are
 * the PC's), flags (bit 0: do not read the FIFO until the window ends - the overrun test), the window in cycles, a pre-delay.
 * 2. The runner's SIO1 settings are saved and these written; MARK A1h ("armed": the PC starts the stimulus when its Pico sees it).
 * 3. For the window: SIO_STAT polled (bits 0-9; 11+ are the baud counter), each change recorded with Timer 2's count since the arm
 * (sysclk, free running, accumulated); each byte read recorded with its STAT. 4. At the end: the FIFO drained, the flags
 * acknowledged, MARK A2h, the runner's settings put back, the record printed (E lines) and sent as a stage 41h frame.
 * Nothing is printed while the window is open (SIO1 is under test). No assertions: compare with the emulator's run of the same.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include "../dbio/dbio.h"

#define SIO_DATA (*(volatile uint8_t *)0x1F801050u)
#define SIO_STAT (*(volatile uint32_t *)0x1F801054u)
#define SIO_MODE (*(volatile uint16_t *)0x1F801058u)
#define SIO_CTRL (*(volatile uint16_t *)0x1F80105Au)
#define SIO_BAUD (*(volatile uint16_t *)0x1F80105Eu)
#define T2_CNT   (*(volatile uint16_t *)0x1F801120u)
#define T2_MODE  (*(volatile uint16_t *)0x1F801124u)
#define NEV 6000

struct Ev { uint32_t t; uint16_t stat; uint16_t data; };   /* data: 0x100 | byte for a read, 0 for a status change */
static Ev ev[NEV];

int main() {
    printf("\nsio1/sio1-stim\n");
    uint8_t p[16];
    for (int i = 0; i < 16; i++) p[i] = 0;
    if (!db_input(p, 16, 6000000)) { printf("no settings over the DB-25 (no GameShark Pico, or nothing queued)\nsio1/sio1-stim: Done.\n"); for (;;) VSync(0); }
    const uint16_t baud = p[0] | (p[1] << 8), mode = p[2] | (p[3] << 8), ctrl = p[4] | (p[5] << 8), flags = p[6] | (p[7] << 8);
    const uint32_t window = p[8] | (p[9] << 8) | (p[10] << 16) | ((uint32_t)p[11] << 24);
    const uint32_t pre = p[12] | (p[13] << 8) | (p[14] << 16) | ((uint32_t)p[15] << 24);
    printf("settings: BAUD %u MODE %x CTRL %x flags %x window %u cycles, pre %u\n", baud, mode, ctrl, flags, (unsigned)window, (unsigned)pre);

    EnterCriticalSection();
    const uint16_t m0 = SIO_MODE, c0 = SIO_CTRL, b0 = SIO_BAUD, t2m = T2_MODE;
    T2_MODE = 0;   /* free running, sysclk */
    SIO_CTRL = 0x40;   /* reset, then as asked */
    SIO_MODE = mode; SIO_BAUD = baud; SIO_CTRL = ctrl;
    { uint16_t l = T2_CNT; uint32_t s = 0; while (s < pre) { const uint16_t t = T2_CNT; s += (uint16_t)(t - l); l = t; } }
    db_mark(0xA1);
    uint32_t n = 0, acc = 0, lastStat = 0xFFFFFFFFu;
    uint16_t last = T2_CNT;
    const bool hold = flags & 1;
    while (acc < window && n < NEV - 16) {
        const uint16_t t = T2_CNT; acc += (uint16_t)(t - last); last = t;
        const uint32_t st = SIO_STAT & 0x3FFu;
        if (st != lastStat) { ev[n].t = acc; ev[n].stat = (uint16_t)st; ev[n].data = 0; n++; lastStat = st; }
        if (!hold && (st & 2)) { const uint8_t d = SIO_DATA; ev[n].t = acc; ev[n].stat = (uint16_t)st; ev[n].data = 0x100 | d; n++; }
    }
    /* the end: drain what is left (the overrun test reads it here), then acknowledge the flags */
    for (int k = 0; k < 16 && n < NEV; k++) {
        const uint16_t t = T2_CNT; acc += (uint16_t)(t - last); last = t;
        const uint32_t st = SIO_STAT & 0x3FFu;
        if (!(st & 2)) { ev[n].t = acc; ev[n].stat = (uint16_t)st; ev[n].data = 0x200; n++; break; }
        const uint8_t d = SIO_DATA; ev[n].t = acc; ev[n].stat = (uint16_t)st; ev[n].data = 0x300 | d; n++;
    }
    db_mark(0xA2);
    SIO_CTRL = 0x40; SIO_MODE = m0; SIO_BAUD = b0; SIO_CTRL = c0 | 0x10;   /* the runner's settings, flags acknowledged */
    T2_MODE = t2m;
    ExitCriticalSection();

    /* E <t cycles> <stat hex> <what>: '-' a status change, 'r xx' a read in the window, 'd xx' drained at the end, 'e' empty */
    printf("events %u\n", (unsigned)n);
    for (uint32_t i = 0; i < n; i++) {
        const uint16_t k = ev[i].data >> 8;
        if (k == 0) printf("E %u %x -\n", (unsigned)ev[i].t, ev[i].stat);
        else if (k == 1) printf("E %u %x r %02x\n", (unsigned)ev[i].t, ev[i].stat, ev[i].data & 0xFF);
        else if (k == 2) printf("E %u %x e\n", (unsigned)ev[i].t, ev[i].stat);
        else printf("E %u %x d %02x\n", (unsigned)ev[i].t, ev[i].stat, ev[i].data & 0xFF);
    }
    db_report(0x41, (const uint32_t *)ev, n * 2);
    printf("sio1/sio1-stim: Done.\n");
    for (;;) VSync(0);
}
