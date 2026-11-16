/*
 * hwtests/stamp-template/stamp.h - THE STAMP-AND-SCATTER INSTRUMENT, as one header (2026-11-16, the SPU session; built six times by
 * hand that week: spu-readtime, spu-readstart, spu-writetime, spu-irqslot, spu-envxslot, spu-capirq*). Copy the template's folder,
 * keep this header beside your main.cpp (`#include "stamp.h"`), and write only the trial.
 *
 * WHAT IT GIVES YOU
 *   stamp_begin() / stamp_end()  Timer 2 at the system clock (mode 0), its old mode put back after.
 *   now()                        one UNWRAPPED cycle count: Timer 2 wraps every 65,536 cycles, so call now() (or stamp_t()) at
 *                                least that often, and every count printed is on this one clock.
 *   idle(c)                      wait c cycles on that clock.
 *   scatter(n)                   0..47 single cycles more (fine()): every wait ends on a poll loop's turn, and a wait on an SPU
 *                                or GPU status flag re-syncs those turns to the device's clock - without single-cycle scatter the
 *                                trials fall on ~25 places of a 768-cycle sample instead of all of them (spu-latch2's lesson).
 *   rnd(n)                       a fixed LCG: the same trial times on every console and the emulator, so runs line up trial by trial.
 *   STAMP_LOOP(n, reg)           THE ARRAY LOOP: n turns that only read Timer 2 and one register into two arrays (~22 cycles a
 *                                turn on a 16-bit I/O register); read them afterwards with stamp_scan(). A step of the register
 *                                is placed between two consecutive reads, and a DMA block's bus time shows as one long turn.
 *   stamp_scan(t0, cb)           walks the arrays on the unwrapped clock: cb(i, t, value, turnLength) for each turn whose value
 *                                differs from the turn before (the first turn compared with what the register held before).
 *   pr_*                         print helpers that call now() between pieces.
 *
 * ITS TWO TRAPS, BUILT IN
 *   1. A long printf can take more than 65,536 cycles on the console's serial TTY (and the emulator's): the count loses a wrap and
 *      every later stamp is 65,536 off. Print a line in pieces with now() between them (pr_u, pr_d, pr_x, pr_nl), and never print
 *      inside a timed stretch. 1b: nor inside stamp_scan's callback - now() moves the clock's last-read point and every array
 *      entry after it is then counted a wrap off. Collect in the callback, print after the scan.
 *   2. The compiler moves work it does not see a dependency for INTO the timed window (a buffer clear, two divisions and the DMA
 *      register writes once sat between SPUSTAT showing a mode and the CHCR write, so no trial came sooner than 280 cycles). Do the
 *      setup BEFORE the first store of the stretch, keep helpers that touch big buffers noinline (STAMP_NOINLINE), and read the
 *      emulator's stamps of the stretch first: if its shortest is far above what you asked, something moved in.
 */
#pragma once
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define STAMP_NOINLINE __attribute__((noinline))

static uint32_t stamp_clk; static uint16_t stamp_last; static uint16_t stamp_mode;
static inline uint32_t stamp_t(uint16_t t) { stamp_clk += (uint16_t)(t - stamp_last); stamp_last = t; return stamp_clk; }
static inline uint32_t now() { return stamp_t(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static void stamp_begin() { stamp_mode = read16(T2_MODE); write16(T2_MODE, 0); stamp_last = read16(T2_COUNT); stamp_clk = 0; }
static void stamp_end() { write16(T2_MODE, stamp_mode); }

static uint32_t stamp_lcg = 12345;
static uint32_t rnd(uint32_t n) { stamp_lcg = stamp_lcg * 1664525u + 1013904223u; return (stamp_lcg >> 10) % n; }
static void STAMP_NOINLINE scatter(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}

#ifndef STAMP_TURNS
#define STAMP_TURNS 1600
#endif
static uint16_t stamp_tt[STAMP_TURNS], stamp_vv[STAMP_TURNS];
static int stamp_n;
/* reg: an I/O address read as 16 bits (a 32-bit register's low half is enough for most flags; read32 and cast if not) */
#define STAMP_LOOP(n, reg) do { stamp_n = (n) < STAMP_TURNS ? (n) : STAMP_TURNS; \
    for (int stamp_i = 0; stamp_i < stamp_n; stamp_i++) { stamp_tt[stamp_i] = read16(T2_COUNT); stamp_vv[stamp_i] = read16(reg); } } while (0)
/* t0: the stamp taken just before the loop; prev: the register's value before it */
template <typename F> static void stamp_scan(uint32_t t0, uint16_t prev, F cb) {
    uint32_t pt = t0;
    for (int i = 0; i < stamp_n; i++) {
        const uint32_t t = stamp_t(stamp_tt[i]);
        if (stamp_vv[i] != prev) { cb(i, t, stamp_vv[i], t - pt); prev = stamp_vv[i]; }
        pt = t;
    }
}

static void pr_s(const char* s) { now(); printf("%s", s); now(); }
static void pr_u(uint32_t v) { now(); printf(" %u", (unsigned)v); now(); }
static void pr_d(int32_t v) { now(); printf(" %d", (int)v); now(); }
static void pr_x(uint32_t v) { now(); printf(" %x", (unsigned)v); now(); }
static void pr_nl() { now(); printf("\n"); now(); }
