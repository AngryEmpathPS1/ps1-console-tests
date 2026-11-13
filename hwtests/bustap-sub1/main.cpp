/*
 * hwtests/bustap-sub1 - THE SUB-BUS, ACCESS BY ACCESS, FOR THE TAP (2026-11-12; docs/BUSTAP.md, pico-bustap/).
 *
 * WHY THIS TEST EXISTS. Every sub-bus timing this core models - the BIOS ROM's, the CD's and the SPU's Delay/Size
 * registers, COM_DELAY's terms, a store's bus time in the write buffer, the recovery a slow read leaves (src/psx.js
 * slowAccessB2B, slowWriteBusTimes, slowRecovery) - was fitted to TOTALS: a timer count around a loop of a hundred
 * loads (hwtests/mem-delay, cpu/access-time, slow-b2b). The tap on the parallel port's pads sees every strobe of every
 * access, so the same accesses can be read one by one: how long each /RD and /WR is low, how far apart they are, where
 * a store's bus cycle lands while the CPU runs on, what COM0 adds and where. That is the sub-bus half of the load/store
 * residuals and the per-register terms nothing has measured access by access.
 *
 * WHAT IT DOES. Eight blocks, run back to back with interrupts off, each the same asm sequence between two markers the
 * tap sees: a byte store to 1F0000k0h (block k's START: the first one is the tap's trigger) and one to 1F0000k4h (its
 * END) - EXP1 writes, /EXP low, the block's number on A4-A7. Between them: BIOS ROM reads (8, 16, 32 bits, back to
 * back and alone), the CD's status register twice, SPUSTAT twice, a 32-bit SPU read, two SPU halfword stores and a
 * word store (voice 23's volume, a voice that is off), an SPU read right behind them, the CD's index written back as
 * it was, an EXP1 read (nothing fitted: open bus), an EXP2 read and a POST write (as the BIOS does at boot).
 *   0  the kernel's values, the code cached and the I-cache cold     4  COM_DELAY 00001323h, CD_DELAY 00020843h
 *   1  the kernel's values, warm                                      5  BIOS_DELAY 0013247Fh (read delay 3 -> 7), SPU 220931E1h
 *   2  COM_DELAY 00001325h, CD_DELAY 00020943h (FF7's)                6  the kernel's values, the code UNCACHED (KSEG1)
 *   3  COM_DELAY 0000132Ch, CD_DELAY 00020943h (FF7's)                7  the kernel's values, warm again (what varies)
 * Every value is one the kernel or a retail game writes (hwtests/mem-delay's list; psx-spx's 220931E1h "for SPU-RAM
 * reads"), each register is put back after its block, and nothing here runs from a region whose timing it changes.
 * The TTY gives each block's length in CPU cycles (Timer 2) - the cross-check without the tap.
 *
 * WITH THE TAP: node tools/bustap.js capture --link=... before the run; tools/bustapdiff.js lines its list up with
 * PREDICTED.bus.txt (tools/bustapemu.js, this core's own sub-bus timeline) block by block. NO cartridge in the
 * parallel port while it runs (its ROM would answer the EXP1 accesses and the markers).
 */
#include <common.h>
#include <psxapi.h>

#define EXP1_DELAY 0x1f801008
#define BIOS_DELAY 0x1f801010
#define SPU_DELAY  0x1f801014
#define CD_DELAY   0x1f801018
#define COM_DELAY  0x1f801020
#define T2_COUNT   0x1f801120
#define T2_MODE    0x1f801124

static inline uint32_t rd(uint32_t a) { return *(volatile uint32_t*)a; }
static inline void wr(uint32_t a, uint32_t v) { *(volatile uint32_t*)a = v; }

/* The block: one asm function, so nothing the compiler places lands inside it. $a0 = the block's marker offset (k << 4).
 * Position-independent (absolute data addresses, no jumps but the return), so it runs from KSEG1 as well. */
extern "C" void bus_block(uint32_t marker);
asm(R"(
.set push
.set noreorder
.text
.globl bus_block
.type bus_block, @function
bus_block:
    lui   $t0, 0x1f00            # EXP1
    lui   $t1, 0x1fc0            # BIOS ROM
    lui   $t2, 0x1f80            # the I/O block
    addu  $t3, $t0, $a0          # 1F0000k0h
    sb    $zero, 0($t3)          # START (the first is the tap's trigger)
    nop
    nop
    nop
    nop
    lbu   $v0, 0x100($t1)        # BIOS: a byte (1 bus cycle)
    lhu   $v0, 0x102($t1)        #       a halfword (2)
    lw    $v0, 0x104($t1)        #       a word (4)
    lw    $v0, 0x108($t1)        #       a word, back to back
    nop
    nop
    nop
    nop
    lw    $v0, 0x10c($t1)        #       a word alone
    lbu   $v0, 0x1800($t2)       # CD: the status register
    lbu   $v0, 0x1800($t2)       #     back to back
    lhu   $v0, 0x1dae($t2)       # SPU: SPUSTAT
    lhu   $v0, 0x1dae($t2)       #      back to back
    lw    $v0, 0x1dac($t2)       #      transfer control + SPUSTAT (2 bus cycles)
    sh    $zero, 0x1d70($t2)     #      voice 23 volume left (the voice is off)
    sh    $zero, 0x1d72($t2)     #      and right, back to back
    sw    $zero, 0x1d70($t2)     #      both in one store (2 bus cycles)
    lhu   $v0, 0x1dae($t2)       #      a read right behind the stores
    lbu   $v1, 0x1800($t2)       # CD: the status again, for the index
    nop                          #     (the load delay)
    andi  $v1, $v1, 3
    sb    $v1, 0x1800($t2)       #     the index written back as it was
    lbu   $v0, 0x100($t0)        # EXP1: a byte (open bus)
    lbu   $v0, 0x2000($t2)       # EXP2: a byte
    sb    $zero, 0x2041($t2)     # EXP2: the POST register (the BIOS writes it at boot)
    nop
    nop
    sb    $zero, 4($t3)          # END (1F0000k4h)
    jr    $ra
    nop
.size bus_block, .-bus_block
.set pop
)");

struct Setting { const char* what; uint32_t com, cd, spu, bios; int uncached; };
#define K 0xffffffffu   /* the kernel's value, untouched */
static const Setting BLOCKS[] = {
    { "kernel, cached, cold I-cache", K, K, K, K, 0 },
    { "kernel, cached, warm", K, K, K, K, 0 },
    { "COM 1325 CD 20943 (FF7)", 0x1325, 0x20943, K, K, 0 },
    { "COM 132C CD 20943 (FF7)", 0x132c, 0x20943, K, K, 0 },
    { "COM 1323 CD 20843", 0x1323, 0x20843, K, K, 0 },
    { "BIOS 0013247F SPU 220931E1", K, K, 0x220931e1, 0x0013247f, 0 },
    { "kernel, UNCACHED (KSEG1)", K, K, K, K, 1 },
    { "kernel, cached, warm again", K, K, K, K, 0 },
};
#define NB (sizeof(BLOCKS) / sizeof(BLOCKS[0]))

int main() {
    printf("\nbustap-sub1\n");
    printf("Sub-bus accesses between EXP1 markers, for the parallel-port tap (docs/BUSTAP.md).\n");
    printf("No assertions - the tap's capture is the measurement; Timer 2 gives each block's total.\n\n");

    const uint32_t com0 = rd(COM_DELAY), cd0 = rd(CD_DELAY), spu0 = rd(SPU_DELAY), bios0 = rd(BIOS_DELAY), exp0 = rd(EXP1_DELAY);
    printf("  as the kernel left them: EXP1=%08x BIOS=%08x SPU=%08x CD=%08x COM=%08x\n\n", exp0, bios0, spu0, cd0, com0);

    static uint32_t cycles[NB];
    EnterCriticalSection();
    const uint16_t t2mode = *(volatile uint16_t*)T2_MODE;
    *(volatile uint16_t*)T2_MODE = 0x0000;   /* Timer 2: the system clock, free-running */
    for (unsigned k = 0; k < NB; k++) {
        const Setting& s = BLOCKS[k];
        if (s.com != K) wr(COM_DELAY, s.com);
        if (s.cd != K) wr(CD_DELAY, s.cd);
        if (s.spu != K) wr(SPU_DELAY, s.spu);
        if (s.bios != K) wr(BIOS_DELAY, s.bios);
        void (*fn)(uint32_t) = s.uncached ? (void (*)(uint32_t))((uint32_t)&bus_block | 0xa0000000u) : bus_block;
        *(volatile uint16_t*)T2_COUNT = 0;
        fn(k << 4);
        cycles[k] = *(volatile uint16_t*)T2_COUNT;
        wr(COM_DELAY, com0); wr(CD_DELAY, cd0); wr(SPU_DELAY, spu0); wr(BIOS_DELAY, bios0);
    }
    *(volatile uint16_t*)T2_MODE = t2mode;
    ExitCriticalSection();

    for (unsigned k = 0; k < NB; k++)
        printf("  block %u  markers 1F0000%x0h/%x4h  %5u cycles  %s\n", k, k, k, cycles[k], BLOCKS[k].what);
    printf("\n  restored: BIOS=%08x SPU=%08x CD=%08x COM=%08x\n", rd(BIOS_DELAY), rd(SPU_DELAY), rd(CD_DELAY), rd(COM_DELAY));
    printf("\nDone.\n");
    for (;;) {}
}
