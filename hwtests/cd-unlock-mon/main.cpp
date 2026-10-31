/*
 * cdrom/cd-unlock-mon - NOT A TEST: hwtests/cd-unlock for a console under nugget's monitor (2026-10-31).
 *
 * WHY. cd-unlock returns from main, which under the runner ends the run. Under the monitor a program starts with every register 0,
 * so a return goes to address 0, and a program that spins with its interrupts off leaves the monitor deaf until RESET
 * (hwtests/store-stream did, on the SCPH-5501 #2): the monitor reads the link for STOP only when an interrupt is taken. This
 * leaves by `break 4, 0`, which the monitor stops on, and the drive stays unlocked for the next program: psx-spx says the state
 * survives everything but the drive's Reset (1Ch) and the HCRISD bit.
 *
 * Not in CONSOLE-RUN-SHEET.md's table, as cd-unlock is not: no console owes a log of it, nothing grades it.
 */
#define main cdUnlockMain
#include "../cd-unlock/main.cpp"
#undef main

int main() {
    cdUnlockMain();
    __asm__ volatile("break 4, 0");
    for (;;) {}
}
