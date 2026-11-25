/*
 * exp1-trains (2026-11-25, the cart session, for the cold-io session's EXP1 write model) - STORES TO EXPANSION 1 IN TRAINS, TIMED FROM OUTSIDE THE CONSOLE. Each train is 128 stores to 1F060008h (the cart's comms-out register: every store
 * pulses the cart's ACK, which the DB-25 Pico stamps on its own crystal) in the shape of the CLOCK mode's trains (cartrom/modes2.inc clock_train: code written to RAM uncached, run cached, the first pass cold, the second warm; unrolled, no loop), at the kernel's EXP1 delay
 * (0013243Fh) and COM delay (00031125h), with the Pico's marks (a byte 0A0h-0DFh stored to 1F060008h) before and after the warm pass:  mark start tag = 0B0h + train, end tag = 0C8h + train (never 0AFh: the Pico takes that byte for the console's request). The host (tools/exp1trains.js) turns the Pico's MARK lines into ns per store; this program also prints Timer 2's cycles for each warm pass (it carries the two bracketing reads).
 * The trains (id: shape):
 *   0-10   sb to 1F060008h, then N nops: N = 0 1 2 3 4 6 8 12 16 24, and 8 again (id 10, to tie to the CLOCK mode's 8-nop train)
 *   11-14  sh and sw to 1F060008h, 0 and 8 nops (is a 16- or 32-bit store on the 8-bit bus 2x or 4x the byte?)
 *   15-16  sb EXP1, then sb to a RAM word, 0 and 8 nops before the next EXP1 store (does a RAM store wait behind the EXP1 drain?)
 *   17-18  sb EXP1, then lbu from RAM, 0 and 8 nops (does a RAM read wait for the drain?)
 *   19-20  sb EXP1, then lbu from the flash at 1F000100h (EXP1 again), 0 and 8 nops (does a read of the same region wait?)
 *   21-23  the same with 2, 4 and 6 nops after the read (where the read's tail ends)
 * It reads nothing but the flash byte, writes nothing but 1F060008h and one RAM word, and needs the cart ON (the stores must reach its comms register to pulse ACK) and a DB-25 Pico on it for the marks.
 */
#include <common.h>
#include <psxapi.h>
#include <stdio.h>

#define EXP1_DELAY (*(volatile uint32_t *)0x1F801008u)
#define COM_DELAY  (*(volatile uint32_t *)0x1F801020u)
#define I_MASK     (*(volatile uint32_t *)0x1F801074u)
#define T2         (*(volatile uint16_t *)0x1F801120u)
#define T2_MODE    (*(volatile uint16_t *)0x1F801124u)
#define MARK(t)    (*(volatile uint8_t *)0xBF060008u = (uint8_t)(t))

enum { SB, SH, SW };
struct Train { int store; int second; int nops; const char *name; };   // second: 0 none, 1 sb RAM, 2 lbu RAM, 3 lbu EXP1
static const Train T[] = {
    { SB, 0, 0, "sb 0 nops" }, { SB, 0, 1, "sb 1 nop" }, { SB, 0, 2, "sb 2 nops" }, { SB, 0, 3, "sb 3 nops" }, { SB, 0, 4, "sb 4 nops" }, { SB, 0, 6, "sb 6 nops" },
    { SB, 0, 8, "sb 8 nops" }, { SB, 0, 12, "sb 12 nops" }, { SB, 0, 16, "sb 16 nops" }, { SB, 0, 24, "sb 24 nops" }, { SB, 0, 8, "sb 8 nops (again)" },
    { SH, 0, 0, "sh 0 nops" }, { SH, 0, 8, "sh 8 nops" }, { SW, 0, 0, "sw 0 nops" }, { SW, 0, 8, "sw 8 nops" },
    { SB, 1, 0, "sb EXP1 + sb RAM, 0 nops" }, { SB, 1, 8, "sb EXP1 + sb RAM, 8 nops" },
    { SB, 2, 0, "sb EXP1 + lbu RAM, 0 nops" }, { SB, 2, 8, "sb EXP1 + lbu RAM, 8 nops" },
    { SB, 3, 0, "sb EXP1 + lbu EXP1, 0 nops" }, { SB, 3, 8, "sb EXP1 + lbu EXP1, 8 nops" },
    { SB, 3, 2, "sb EXP1 + lbu EXP1, 2 nops" }, { SB, 3, 4, "sb EXP1 + lbu EXP1, 4 nops" }, { SB, 3, 6, "sb EXP1 + lbu EXP1, 6 nops" },   // 21-23 (cold-io: splits the read's tail)
};
#define NT ((int)(sizeof T / sizeof T[0]))

// BIOS A(44h) FlushCache: the I-cache must not hold the previous train
static inline void flush_cache() { __asm__ volatile("li $t2, 0xA0\n li $t1, 0x44\n jalr $t2\n nop" ::: "t0","t1","t2","t3","t4","t5","t6","t7","t8","t9","a0","a1","a2","a3","v0","v1","ra","memory"); }
static uint32_t code[4096] __attribute__((aligned(64)));   // generated trains: 128 x (1 + 1 + 24) words at most = 3,328
static volatile uint32_t ramword[4] __attribute__((aligned(16)));

// writes the train's code into the (uncached alias of the) buffer, returns its length in words
static int build(const Train &t, int tag) {
    volatile uint32_t *r = (volatile uint32_t *)(((uint32_t)code & 0x1FFFFFFFu) | 0xA0000000u);
    int k = 0;
    r[k++] = 0x3C081F06u;                          // lui t0, 1F06h
    r[k++] = 0x34090055u;                          // ori t1, zero, 55h: the stores themselves write a byte below 0A0h (not a mark: a mark line is ~30 bytes of text, 128 of them a train overran the Pico's 16 KB ring and lost the later trains' marks); only the start and end marks use the tags
    r[k++] = 0x00805021u;                          // addu t2, a0, zero   (a0: a RAM word's address)
    r[k++] = 0x3C0C1F00u;                          // lui t4, 1F00h
    const uint32_t st = t.store == SB ? 0xA1090008u : t.store == SH ? 0xA5090008u : 0xAD090008u;   // sb / sh / sw t1, 8(t0)
    for (int i = 0; i < 128; i++) {
        r[k++] = st;
        if (t.second == 1) r[k++] = 0xA1490000u;   // sb t1, 0(t2)
        if (t.second == 2) r[k++] = 0x914B0000u;   // lbu t3, 0(t2)
        if (t.second == 3) r[k++] = 0x918B0100u;   // lbu t3, 100h(t4)
        for (int g = 0; g < t.nops; g++) r[k++] = 0;
    }
    r[k++] = 0x03E00008u; r[k++] = 0;              // jr ra; nop
    return k;
}

int main() {
    printf("\nexp1-trains\n");
    T2_MODE = 0;                                   // Timer 2: system clock, free run
    const uint32_t e1 = EXP1_DELAY, com = COM_DELAY, im = I_MASK;
    printf("EXP1 delay %08X, COM_DELAY %08X on entry; the trains run at 0013243Fh and 00031125h (the kernel's)\n", (unsigned)e1, (unsigned)com);
    printf("id name                         Timer 2 cycles for 128 stores (warm), per store\n");
    for (int i = 0; i < NT; i++) {
        build(T[i], 0xB0 + i);                     // the start and end marks are the only mark bytes
        flush_cache();
        void (*f)(uint32_t) = (void (*)(uint32_t))(((uint32_t)code & 0x1FFFFFFFu) | 0x80000000u);
        I_MASK = 0; EXP1_DELAY = 0x0013243Fu; COM_DELAY = 0x00031125u;
        f((uint32_t)ramword);                      // cold pass
        MARK(0xB0 + i);
        const uint16_t a = T2;
        f((uint32_t)ramword);                      // warm pass
        const uint16_t b = T2;
        MARK(0xC8 + i);
        EXP1_DELAY = e1; COM_DELAY = com; I_MASK = im;
        const uint32_t cyc = (uint16_t)(b - a);
        printf("%2d %-28s %6u  %u.%02u\n", i, T[i].name, (unsigned)cyc, (unsigned)(cyc / 128), (unsigned)((cyc * 100 / 128) % 100));
    }
    printf("Done.\n");
    for (;;) {}
}
