/*
  * dbio - A TEST THAT TAKES ITS INPUTS FROM THE PC AND REPORTS OUT THE DB-25, with SIO1 never touched (cart ROM v5; owner AngryEmpath, 2026-11-17: "feed a running test its inputs",
 * "results out the DB-25 so SIO1 itself can be tested"). Not a measurement: the check that hwtests/dbio/dbio.h works end to end through the cart's CART START (an uploaded program) and the
 * Pico's feed. It marks (A0h at the start, B0h + the byte count after the input, A2h at the end), asks the PC for 16 bytes (db_input), and reports them back in a stage 40h frame with
 * their sum and the cycles Timer 2 counted for the input. Its printout (the CART START log) says what it got; with no Pico it says so and still ends with Done.
 *   node tools/dbsend.js --upload hwtests/build/dbio.exe --queue <16 bytes in hex>      (then power on with the GameShark; tools/dbcaptest.js does the same on the emulator)
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>
#include "dbio.h"

int main() {
    printf("\nsio/dbio\n");
    uint8_t in[16];
    for (int i = 0; i < 16; i++) in[i] = 0xEE;
    db_mark(0xA0);
    const uint32_t t0 = *(volatile uint16_t *)0x1F801120;
    const int ok = db_input(in, 16);
    const uint32_t t1 = *(volatile uint16_t *)0x1F801120;
    db_mark(0xB0);
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += in[i];
    uint32_t w[8] = { (uint32_t)ok, sum, (t1 - t0) & 0xFFFF, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) w[3 + i] = in[4 * i] | (in[4 * i + 1] << 8) | (in[4 * i + 2] << 16) | ((uint32_t)in[4 * i + 3] << 24);
    const int sent = db_report(0x40, w, 7);
    printf("db_input %s; the bytes:", ok ? "ok" : "NOTHING CAME");
    for (int i = 0; i < 16; i++) printf(" %02x", in[i]);
    printf("; sum %d; report %s\n", (int)sum, sent ? "sent" : "NOT sent (no Pico)");
    db_mark(0xA2);
    printf("sio/dbio: Done.\n");
    for (;;) VSync(0);
}
