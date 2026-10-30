/*
 * cpu/cpu-loop-tick12 - a slow-bus read (the CD status register, SPUSTAT) behind queued RAM stores (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick10 on the SCPH-9001: two stores (sw; sh) cost an SPUSTAT read 5.11 cycles a turn, this core 7.33
 * - the read waits for the write queue less IO_WB_OVERLAP as an on-die read does; a cycle less fits (experiments/
 * _mkslowov211.js). The CD's registers sit on the same bus behind the same queue in this core's model, unmeasured there: this
 * asks the CD's status register (1F801800h, a read that changes nothing) the same, and both after one store, two pages'
 * stores, and with nothing waiting for the value.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; every loop ends `addiu; bnez; nop`. LD = lhu v1, A; lw a1, A+4. R = the read (lbu for the CD, lhu for the SPU) of v0;
 * andi v0.
 *   CD-LD / CD-ST-LD     R; LD  /  sw A+4; sh A; R; LD
 *   CD-ST / CD-1ST       sw A+4; sh A; R  /  sw A+4; R
 *   CD-FAR               sw A+4; sw A+16K; R               two pages' first words
 *   CD-ST-NOW            sw A+4; sh A; lbu v0, CD; 3 nops  nothing reading the value
 *   SPU-...              the same six with SPUSTAT
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick12 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define CDSTAT   0x1F801800u
#define SPUSTAT  0x1F801DAEu

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define LD "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n"
#define ST2 "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n"
#define CDR "  lbu $v0, 0(%1)\n  andi $v0, $v0, 0xff\n"
#define SPR "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n"
LOOP(cdLd, CDR LD)
LOOP(cdStLd, ST2 CDR LD)
LOOP(cdSt, ST2 CDR)
LOOP(cd1St, "  sw $v0, 4(%2)\n" CDR)
LOOP(cdFar, "  sw $v0, 4(%2)\n  sw $v0, 0x4000(%2)\n" CDR)
LOOP(cdStNow, ST2 "  lbu $v0, 0(%1)\n  nop\n  nop\n  nop\n")
LOOP(spLd, SPR LD)
LOOP(spStLd, ST2 SPR LD)
LOOP(spSt, ST2 SPR)
LOOP(sp1St, "  sw $v0, 4(%2)\n" SPR)
LOOP(spFar, "  sw $v0, 4(%2)\n  sw $v0, 0x4000(%2)\n" SPR)
LOOP(spStNow, ST2 "  lhu $v0, 0(%1)\n  nop\n  nop\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick12\n");
    printf("A CD / SPU register read behind queued RAM stores. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; };
    static const V vs[] = {
        { "CD-LD", cdLd, CDSTAT }, { "CD-ST-LD", cdStLd, CDSTAT }, { "CD-ST", cdSt, CDSTAT }, { "CD-1ST", cd1St, CDSTAT },
        { "CD-FAR", cdFar, CDSTAT }, { "CD-ST-NOW", cdStNow, CDSTAT },
        { "SPU-LD", spLd, SPUSTAT }, { "SPU-ST-LD", spStLd, SPUSTAT }, { "SPU-ST", spSt, SPUSTAT }, { "SPU-1ST", sp1St, SPUSTAT },
        { "SPU-FAR", spFar, SPUSTAT }, { "SPU-ST-NOW", spStNow, SPUSTAT },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick12: Done.\n");
    for (;;) VSync(0);
}
