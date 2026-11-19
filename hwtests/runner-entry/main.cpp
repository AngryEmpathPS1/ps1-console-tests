/*
 * runner-entry - THE FIRST WRITE A PROGRAM MAKES, as a marker for the cycle the runner started it (2026-11-19). Not a test of the machine: the runner's start path
 * (the locked landing to the program's first instruction) is measured on the emulator by a trace of Timer writes (experiments/runnerguard/tailmeas.js); this program's write to Timer 0's target is the last event of that trace, so the gap from the runner's own
 * T2_MODE = 0 write to it is the path's length, and two builds of the runner can be compared on it. The write is Timer 0's TARGET (1F801108h), 5A5Ah.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>

int main() {
    *(volatile uint16_t *)0x1F801108 = 0x5A5A;
    printf("\nrunner-entry\nDone.\n");
    return 0;
}
