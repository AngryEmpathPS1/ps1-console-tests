/*
 * cpu/at-split - ps1-tests' cpu/access-time with its two windows apart: the nop loop and the read loop, each measured
 * cold (its first call) and again warm, raw Timer 2 cycles (2026-10-11 night)
 *
 * WHY. access-time prints each row as (read loop - nop loop) / 100. At the runner-era consoles' landing (all five runs
 * locked at refresh phase 0 and identical) v0.377.78 reads 14 of its 51 numbers exact and the rest off by device -
 * EXPANSION2 +6, RAM_SIZE -8, SPUCNT -2..-5, CDROM_STAT +3, EXPANSION1 +2..+4, TIMER0_VAL 16 -6 - the same before and after
 * the refresh model (scratch/coldbus/at_landing.js). A difference cannot say which window is off, nor whether the cold first
 * pass is (a slow read in a line that is still filling; the stream ends at a slow access) or the loop's own turns. This
 * prints all four.
 *
 * WHAT IT DOES. access-time's own measure (ps1-tests common/measure.hpp: EnterCriticalSection, Timer 2 at the system
 * clock, reset, call, read) around access-time's two lambdas - 100 turns of `nop` and of `nop; lbu/lhu/lw <address>` - for
 * its seventeen addresses and three widths. Each lambda is called twice in a row: the first call runs it cold (it never ran
 * before), the second warm. A row: the address and width, then nop cold, nop warm, read cold, read warm (cycles), then
 * access-time's own number from the cold pair and from the warm pair.
 *
 * READ IT LIKE THIS. The warm pair is the loops' turns alone (a read's cost x 100 + the call); cold - warm is the cold first
 * pass. Against the emulator at the same landing (tools/consolegrade.js reproduces the runner's lock), each window by itself.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js at-split`.
 */
#include <common.h>
#include <measure.hpp>

static void doNotOptimize(void* p) { asm volatile("" : : "g"(p) : "memory"); }

struct Four { uint32_t nc, nw, rc, rw; };

template <typename T, uint32_t base_address, int count = 100>
Four splitBitSize() {
    auto nopLoop = [] {
        volatile T* ptr = (T*)(base_address);
        for (int i = 0; i < count; i++) asm("nop");
        doNotOptimize((void*)ptr);
    };
    auto readLoop = [] {
        volatile T* ptr = (T*)(base_address);
        for (int i = 0; i < count; i++) { asm("nop"); *ptr; }
        doNotOptimize((void*)ptr);
    };
    Four f;
    f.nc = measure<MeasureMethod::CpuClock>(nopLoop);
    f.nw = measure<MeasureMethod::CpuClock>(nopLoop);
    f.rc = measure<MeasureMethod::CpuClock>(readLoop);
    f.rw = measure<MeasureMethod::CpuClock>(readLoop);
    return f;
}

static void show(const char* name, uint32_t addr, int bits, Four f, int count) {
    const int32_t c = (int32_t)f.rc - (int32_t)f.nc, w = (int32_t)f.rw - (int32_t)f.nw;
    printf("%-10s %08x %2d   nop %5u %5u   read %5u %5u   cold %d.%02d warm %d.%02d\n", name, (unsigned)addr, bits,
           (unsigned)f.nc, (unsigned)f.nw, (unsigned)f.rc, (unsigned)f.rw, c / count, (c < 0 ? -c : c) % count, w / count,
           (w < 0 ? -w : w) % count);
}

template <uint32_t base_address, int count = 100>
void row(const char* name) {
    show(name, base_address, 8, splitBitSize<uint8_t, base_address, count>(), count);
    show(name, base_address, 16, splitBitSize<uint16_t, base_address, count>(), count);
    show(name, base_address, 32, splitBitSize<uint32_t, base_address, count>(), count);
}

int main() {
    printf("\ncpu/at-split\n");
    printf("access-time's windows apart: nop loop and read loop, cold (first call) and warm, Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    printf("SEGMENT    ADDRESS  W   nop cold warm   read cold warm   (read - nop) / 100\n");
    row<0x80000000>("RAM");
    row<0xBFC00000>("BIOS");
    row<0x1F800000>("SCRATCHPAD");
    row<0x1F000000>("EXPANSION1");
    row<0x1F802000>("EXPANSION2");
    row<0x1FA00000>("EXPANSION3");
    row<0x1F8010F0>("DMAC_CTRL");
    row<0x1F801044>("JOY_STAT");
    row<0x1F801054>("SIO_STAT");
    row<0x1F801060>("RAM_SIZE");
    row<0x1F801070>("I_STAT");
    row<0x1F801100>("TIMER0_VAL");
    row<0x1F801800>("CDROM_STAT");
    row<0x1F801814>("GPUSTAT");
    row<0x1F801824>("MDECSTAT");
    row<0x1F801DAA>("SPUCNT");
    row<0xFFFE0130>("CACHECTRL");
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
