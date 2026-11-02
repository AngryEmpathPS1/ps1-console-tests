/*
 * bus/state-peek - what a console's sticky registers hold, read and touched by nothing (2026-11-01)
 *
 * WHY. dma/chopping read 7,313-7,323 cycles at its 4-word row on every console for a month, and 7,604 on 2026-11-01 on a 5501 that had run tests for a
 * day since its reset: a narrow rectangle's rows had started to cost 3 cycles each. The program had not changed. So something a test left behind had.
 * A runner holds the machine for hours and nothing clears what a test writes to a register no test puts back.
 *
 * WHAT IT DOES. Reads, with no write of any kind: the memory control registers, RAM_SIZE, DPCR, DICR, I_MASK, the timers' modes, GPUSTAT and the GPU's
 * GP1(10h) answers (those need a write to GP1 and are last), the SPU's main control and transfer registers, the CD's status.
 *
 * READ IT LIKE THIS. R address value. No assertions - compare against a console freshly reset, or your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js state-peek --frames=600 --quiet=300`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define GP0 0x1F801810u
#define GP1 0x1F801814u

static const uint32_t regs[] = {
    0x1F801000, 0x1F801004, 0x1F801008, 0x1F80100C, 0x1F801010, 0x1F801014, 0x1F801018, 0x1F80101C, 0x1F801020,
    0x1F801060, 0x1F801070, 0x1F801074, 0x1F8010F0, 0x1F8010F4, 0x1F8010F8, 0x1F8010FC,
    0x1F801080, 0x1F801084, 0x1F801088, 0x1F801090, 0x1F801094, 0x1F801098, 0x1F8010A0, 0x1F8010A4, 0x1F8010A8, 0x1F8010B0, 0x1F8010B4, 0x1F8010B8,
    0x1F8010C0, 0x1F8010C4, 0x1F8010C8, 0x1F8010D0, 0x1F8010D4, 0x1F8010D8, 0x1F8010E0, 0x1F8010E4, 0x1F8010E8,
    0x1F801104, 0x1F801108, 0x1F801114, 0x1F801118, 0x1F801124, 0x1F801128, 0x1F801814, 0xFFFE0130
};

int main() {
    printf("\nbus/state-peek\n");
    printf("R address value. No assertions.\n\n");
    EnterCriticalSection();
    static uint32_t got[sizeof(regs) / sizeof(regs[0])];
    const int n = sizeof(regs) / sizeof(regs[0]);
    for (int i = 0; i < n; i++) got[i] = read32(regs[i]);
    static uint16_t sp[8];
    sp[0] = read16(0x1F801DAAu); sp[1] = read16(0x1F801DACu); sp[2] = read16(0x1F801DAEu); sp[3] = read16(0x1F801DA6u);
    static uint32_t ga[8];
    for (int i = 0; i < 8; i++) { write32(GP1, 0x10000000u | (i + 2)); ga[i] = read32(GP0); }
    ExitCriticalSection();
    for (int i = 0; i < n; i++) printf("R %x %x\n", regs[i], got[i]);
    for (int i = 0; i < 4; i++) printf("S %d %x\n", i, sp[i]);
    for (int i = 0; i < 8; i++) printf("G %d %x\n", i + 2, ga[i]);
    printf("\nbus/state-peek: Done.\n");
    for (;;) VSync(0);
}
