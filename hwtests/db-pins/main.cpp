/*
 * db-pins (2026-11-23, the cart session) - WHICH CART PIN MOVES WITH WHICH BIT: one byte at a time to the cart's comms-out register 1F060008h (a store pulses the cart's ACK and puts the byte on the
 * comms lines): 01h, 02h, 04h ... 80h, then 00h, FFh, each ~2 ms apart and the sequence twice, with the Pico's pin logger ('L' on its serial port, GP0-GP9 with microsecond stamps) armed. Each byte
 * should raise one Pico data line (DATA0-DATA7 = GP0-GP7) and pulse GP8 (ACK): a line that never moves, or one that moves for the wrong bit, is a wire or a pin in the wrong place.
 * It reads nothing, waits for no STB and touches nothing but 1F060008h. The cart must be on (GameShark / Power Replay switch ON) and a DB-25 Pico on it.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>

static void wait_ms(int ms) { for (volatile int i = 0; i < ms * 4000; i++) {} }

int main() {
    volatile uint8_t *out = (volatile uint8_t *)0x1F060008;
    printf("\ndb-pins: bytes 01 02 04 08 10 20 40 80 00 FF, twice, to 1F060008h\n");
    for (int rep = 0; rep < 2; rep++) {
        for (int b = 0; b < 8; b++) { *out = (uint8_t)(1u << b); wait_ms(2); }
        *out = 0x00; wait_ms(2);
        *out = 0xFF; wait_ms(2);
        *out = 0x00; wait_ms(2);
    }
    printf("Done.\n");
    return 0;
}
