/*
 * hwtests/dbio/dbio.h - A TEST'S OWN DOOR TO THE GAMESHARK'S DB-25 PORT (cart ROM v5; owner AngryEmpath, 2026-11-17). Header only; a test includes it:
 *   #include "../dbio/dbio.h"
 * Only the GameShark has the port (the Power Replay has none: db_present() says 0 there and the stores below go nowhere), and only a Pico on it (pico-dbcap/, README "The DB-25 pipe")
 * hears. What it is for, each of AngryEmpath's picks:
 *   MARKS      db_mark(tag): one byte, 0A0h-0DFh, written to 1F060008h - an ACK pulse the Pico timestamps on its own crystal (13 ns). Written before and after an operation, the
 *              Pico's "MARK <tag> <ns>" lines are that operation's duration measured OUTSIDE the console; written at each checkpoint (a case's start) they are also where a hang
 *              was: the last mark the Pico heard. Outside a frame only (db_report finishes its frame before it returns). A store to the expansion bus costs the test about 8 cycles
 *              (CLOCK mode's trains, this emulator's model): keep it out of what the test times unless that is the point.
 *   RESULTS    db_report(stage, words, n): a standard AESNAP01 frame (tools/cartsnap.js reads it) out the DB-25, whatever the test has done to SIO1 - an SIO1 test at an odd baud rate or a
 *              loopback still gets its numbers out, and a wedged SIO1 does not lose them.
 *   INPUTS     db_input(dst, n): n bytes from the PC, asked for the way the cart asks (cartrom/rom.c db_req / db_get): the request goes out, the Pico drops STB, then presents the n bytes
 *              the host queued (tools/dbsend.js --queue). 1 on success; 0 if no Pico answered or the host had not queued them in time.
 * Every wait is bounded. None of it touches SIO1, the pad or the CD.
 */
#ifndef DBIO_H
#define DBIO_H
#include <stdint.h>

#define DBIO_STB (*(volatile uint8_t *)0x1F020010u)    /* bit 0: the Pico's STB (the cart's own: a byte is waiting / it has room) */
#define DBIO_IN  (*(volatile uint8_t *)0x1F060000u)    /* the PC's byte */
#define DBIO_OUT (*(volatile uint8_t *)0x1F060008u)    /* the console's byte; the write pulses ACK */

static inline int db_present(void) { for (uint32_t n = 0; n < 200; n++) if (DBIO_STB & 1) return 1; return 0; }
static inline void db_mark(uint32_t tag) { DBIO_OUT = (uint8_t)tag; }
static inline int db_put(uint32_t b) {                  /* one byte the usual way: wait for STB, take the PC's byte, write ours; 0: nobody answered */
    for (uint32_t n = 0; !(DBIO_STB & 1); n++) if (n > 2000) return 0;
    (void)DBIO_IN;
    DBIO_OUT = (uint8_t)b;
    return 1;
}
static inline void db_put32(uint32_t v, uint32_t *sum) { for (int i = 0; i < 4; i++) { const uint32_t b = (v >> (8 * i)) & 0xFF; if (sum) *sum += b; db_put(b); } }
/* a frame: AESNAP01, a STAGE record (stage, version 0, kind 1, mode word 0), one record of words (tag 5Eh), the sum, ENDSNAP - the frames the cart sends */
static inline int db_report(uint32_t stage, const uint32_t *words, uint32_t n) {
    if (!db_present()) return 0;
    static const char magic[] = "AESNAP01", end[] = "\nENDSNAP\n";
    for (int i = 0; i < 8; i++) if (!db_put((uint8_t)magic[i])) return 0;
    uint32_t sum = 0;
    auto rec = [&](uint32_t tag, uint32_t count, const uint32_t *v) {
        const uint8_t h[8] = { (uint8_t)tag, 4, (uint8_t)count, (uint8_t)(count >> 8), 0, 0, 0, 0 };
        for (int i = 0; i < 8; i++) { sum += h[i]; db_put(h[i]); }
        for (uint32_t i = 0; i < count; i++) db_put32(v[i], &sum);
    };
    const uint32_t st[4] = { stage, 0, 1, 0 };
    rec(0x0F, 4, st);
    for (uint32_t o = 0; o < n; o += 4096) rec(0x5E, n - o > 4096 ? 4096 : n - o, words + o);
    sum += 0xFF; db_put(0xFF);
    db_put32(sum, 0);
    for (int i = 0; i < 9; i++) db_put((uint8_t)end[i]);
    return 1;
}
/* n bytes from the PC: the request (AFh, n low, n high), the Pico's STB falling ("I heard"), then each byte on STB's rise. spin: looks at STB to wait for each byte. */
static inline int db_input(uint8_t *dst, uint32_t n, uint32_t spin = 600000) {
    if (!db_present()) return 0;
    if (!db_put(0xAF) || !db_put(n & 0xFF) || !db_put((n >> 8) & 0xFF)) return 0;
    for (uint32_t k = 0; DBIO_STB & 1; k++) if (k > 3000) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = 0;
        while (!(DBIO_STB & 1)) if (++k > (i ? spin : spin * 8)) return 0;
        dst[i] = DBIO_IN;
        DBIO_OUT = 0;
        (void)DBIO_STB; (void)DBIO_STB;
    }
    return 1;
}
#endif
