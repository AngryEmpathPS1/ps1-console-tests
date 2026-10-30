/*
 * hwtests/runner - THE TEST RUNNER: a program that runs our tests on a console with nothing of its own in the way
 * (2026-10-08). Not a test: it is what the tests run under.
 *
 * WHY. On console day every test was sent to Unirom in debug mode and printed through it: each printf character went
 * out SIO1 at 115,200 baud with the CPU waiting (~2,940 cycles a character), Unirom's debug handler listened on the cable
 * by interrupt (a test that turned interrupts off left it deaf; ps1-tests' code-in-io hangs on all three consoles), and
 * each test started at whatever phase of the frame, the line and the drive the upload left it. So a test that prints
 * between its measurements, or measures against the video, read another phase on every console run - and none that an
 * emulator could reproduce (tools/unirun.js, 2026-10-08: the reference run through Unirom moves those rows to another
 * phase, not the console's).
 *
 * WHAT IT DOES. Loaded once by Unirom (SEXE from its menu, no debug mode), it takes over SIO1 and talks a small protocol
 * with tools/hwrunner.js, interrupts off between tests, its own stack inside its own image (80180000h up; a test's stack
 * at the top of RAM cannot reach it).
 * WHERE IT LIVES (measured 2026-10-08, scratch_ramtouch: each program sideloaded with RAM above its image filled with a
 * pattern, every 4 KB page it changed in 1,200-1,500 frames): every hwtests program and every ps1-tests program the
 * console run sends stays below 80125000h (gte-latch-cached's image, 80124944h, the highest), except gpu/bandwidth, whose
 * source keeps a buffer at 801A0000h and wrote 801A0000h-801C6000h - over the first runner (801C0000h) and its log, so
 * that test printed nothing and never ended (and mdec-step24's stack reached 801FE000h). So the kernel's copy is at
 * 80130000h, the log at 80140000h-8017FFFFh, the
 * runner (code, data and its own stack, ~27 KB) at 80180000h, and 801A0000h up is the tests' (and their stack's):
 *   'P'               ping: "PONG"
 *   'L' size bytes sum   a PS-X EXE (its 2 KB header, then its body) into RAM at its load address: "OKAY" / "BAD!"
 *   'R' delay end     run it: its BSS cleared, the I-cache flushed, A(3Ch)/B(3Dh) (putchar) and A(3Eh)/B(3Fh) (puts) pointed at the
 *                     runner, which keeps every character in RAM; then a VBlank edge waited for (interrupts off, I_STAT
 *                     bit 0), `delay` more cycles on Timer 2, the CPU's interrupt state as the runner found it, and the
 *                     program called with its own SP and GP. It ends at its "Done." line (or "Done", or returning from
 *                     main), or at the line `end` names: a mode byte - 0 "Done", 1 the count-th line that starts with
 *                     the given text (cdrom/timing's fifth "double-speed timing:", getloc's "Test ") - with bit 7 set
 *                     if the PC may stop the run by sending 'X' (looked for at each character the test prints; a test
 *                     that uses SIO1 itself runs without it), then for mode 1 the text's length, the text and the
 *                     count. The runner takes the CPU back there, puts the two vectors back, and answers "DONE", how
 *                     many characters the test printed, the cycles the run took (Timer 2's wraps counted at each
 *                     character - so a LOWER BOUND: a test silent for more than 65,536 cycles at a time, or one that
 *                     uses Timer 2 itself, reads short; gpu/bandwidth's 2,000 frames read 1.6M) and how it ended (1 its
 *                     end line, 2 returned from main, 3 stopped by the PC).
 *   'S' video target tol phi phiTol end   (v2) 'R' started to the cycle on the video and the DRAM refresh - "THE LOCKED
 *                     START" below; (v4) phi and phiTol, words, the frame's place within a CPU cycle asked for, in 625ths
 *                     (7FFFFFFFh: any) - "THE VIDEO WITHIN A CYCLE". The answer: 'R''s, then the frames waited, the
 *                     refresh phase, the SPU phase, spuS0, refreshR0, (v4) flags, the vernier's phi bounds, its 120 reads
 *                     (v5: each read's count and its 16-bit offset). (v6) after phiTol two more words: the line in
 *                     1250ths of a cycle (0: this core's two-crystal 2,152.5504) and a GP1(06h) range written before the
 *                     landing (bit 31: apply) - and phi, phiTol and the bounds in 1250ths. (v7) the range word's bit
 *                     30 asks for GPUSTAT bit 31's toggle read beside the tick (its 80 reads after the 120, as theirs),
 *                     and bits 24-26 for a display width before the landing (1-5: 256, 320, 512, 640, 368).
 *   'U' video target tol phi phiTol end   (v3) the same on the video and the SPU's sample clock
 *   'F'               (v2) the refresh table the last probe measured; (v3) and the SPU's phase
 *   'G'               the log: length, bytes, byte sum. The log is 256 KB at 80140000h, below the runner, and the
 *                     kernel's copy (below) the 64 KB under that (a load that would reach 80130000h is refused,
 *                     "BIG!"); a test that prints
 *                     more keeps its first 256 KB, and DONE's count says how much more there was. The first version
 *                     kept 96 KB inside the runner and looked for "Done." in it: mdec-color prints 119,597 bytes, and
 *                     its "Done." never ended the run (the virtual console, 2026-10-08).
 *   'M' addr size     memory: the bytes and their sum (RAM after a test, a test's own result arrays)
 *   'B' value         SIO1's baud register to `value` after "BAUD" goes out (18 = 115,200, Unirom's)
 * Nothing goes over the cable while a test runs, so the printout costs nothing, and every test starts at the same point
 * of the video frame. 'S' and 'U' (v2, v3) start it to the cycle on the video and on the DRAM refresh or the SPU's sample
 * clock too; what stays free is the video's phase within a CPU cycle and the drive's state. tools/hwrunner.js runs the
 * same runner on the virtual console (this emulator), so a console's log and the reference run begin each test the same
 * way.
 */
#include <stdint.h>

#define SIO1_DATA (*(volatile uint8_t*)0x1F801050)
#define SIO1_STAT (*(volatile uint16_t*)0x1F801054)
#define SIO1_MODE (*(volatile uint16_t*)0x1F801058)
#define SIO1_CTRL (*(volatile uint16_t*)0x1F80105A)
#define SIO1_BAUD (*(volatile uint16_t*)0x1F80105E)
#define I_STAT    (*(volatile uint32_t*)0x1F801070)
#define I_MASK    (*(volatile uint32_t*)0x1F801074)
#define T2_COUNT  (*(volatile uint16_t*)0x1F801120)
#define T2_MODE   (*(volatile uint16_t*)0x1F801124)
#define T1_COUNT  (*(volatile uint16_t*)0x1F801110)
#define T1_MODE   (*(volatile uint16_t*)0x1F801114)
#define DPCR      (*(volatile uint32_t*)0x1F8010F0)
#define DICR      (*(volatile uint32_t*)0x1F8010F4)
#define CHCR(n)   (*(volatile uint32_t*)(0x1F801088 + 0x10 * (n)))
#define MADR(n)   (*(volatile uint32_t*)(0x1F801080 + 0x10 * (n)))
#define BCR(n)    (*(volatile uint32_t*)(0x1F801084 + 0x10 * (n)))
#define GP0       (*(volatile uint32_t*)0x1F801810)
#define MDEC1     (*(volatile uint32_t*)0x1F801824)
#define SPU_XADDR (*(volatile uint16_t*)0x1F801DA6)

/*
 * EVERY TEST MEETS THE KERNEL AS THE RUNNER FOUND IT (the virtual console, 2026-10-08): gte-irgb then mdec-color in one
 * session crashed the second - psn00bsdk's ResetGraph hooks its interrupt handler into the kernel's chain, gte-irgb left
 * it there with VBlank enabled in I_MASK, and mdec-color, loaded over gte-irgb's code, took a VBlank through the stale
 * hook before its own was in (the runner's memory written over; mdec-color alone ran exactly). So the kernel's 64 KB
 * (psx-spx: 00000000h-0000FFFFh, the kernel's area - its tables, handler chains, TCBs) is copied at the runner's start
 * and put back before each test, with I_MASK and the DMA registers as they were and every channel stopped. What a test
 * leaves elsewhere (below: the GPU, timers, SPU and memory system put back too) and in the drive stays.
 */
#define SNAP_BASE 0x80130000u
#define GP1       (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT   (*(volatile uint32_t*)0x1F801814)
#define TMODE(n)  (*(volatile uint16_t*)(0x1F801104 + 0x10 * (n)))
#define TTARGET(n) (*(volatile uint16_t*)(0x1F801108 + 0x10 * (n)))
#define SPU_KOFF_LO (*(volatile uint16_t*)0x1F801D8C)
#define SPU_KOFF_HI (*(volatile uint16_t*)0x1F801D8E)
#define SPUCNT    (*(volatile uint16_t*)0x1F801DAA)
#define SPU_XFER  (*(volatile uint16_t*)0x1F801DAC)
#define SPUSTAT   (*(volatile uint16_t*)0x1F801DAE)
#define MEMCTRL(n) (*(volatile uint32_t*)(0x1F801000 + 4 * (n)))   /* expansion bases, the delay/size registers, COM_DELAY */
#define RAMSIZE   (*(volatile uint32_t*)0x1F801060)
#define CACHECTL  (*(volatile uint32_t*)0xFFFE0130)
static uint32_t snapMask, snapDpcr, snapDicr, snapStat, snapMem[9], snapRam, snapCache;
static uint16_t snapTMode[2], snapTTarget[2], snapSpucnt, snapXfer;
static void snapshot() {
    volatile uint32_t *k = (volatile uint32_t *)0x80000000, *s = (volatile uint32_t *)SNAP_BASE;
    for (uint32_t i = 0; i < 0x4000; i++) s[i] = k[i];
    snapMask = I_MASK; snapDpcr = DPCR; snapDicr = DICR; snapStat = GPUSTAT;
    for (int n = 0; n < 2; n++) { snapTMode[n] = TMODE(n) & 0x3FF; snapTTarget[n] = TTARGET(n); }
    for (int n = 0; n < 9; n++) snapMem[n] = MEMCTRL(n);
    snapRam = RAMSIZE; snapCache = CACHECTL; snapSpucnt = SPUCNT; snapXfer = SPU_XFER;
}
/*
 * And the machine around it, where a test's numbers were measured to follow it (the virtual console, 2026-10-08):
 * ps1-tests' timers alone read 29,538,211 cycles; after gte-irgb 30,222,831, after mdec-color 30,493,442, after
 * ack-per-device 29,823,590, four different logs - psn00bsdk's ResetGraph resets the GPU and sets its own display mode
 * and Timer 1's (its VSync timeout counts hblanks), and the timers test counts against both. So the GPU is reset and given
 * the display mode GPUSTAT read at the start (resolution, video mode, depth, interlace, display on/off, DMA direction;
 * the display ranges are the reset's), timers 0 and 1 their modes and targets (a mode write restarts the counter), and
 * every SPU voice is keyed off - and then set to nothing (runner v3): every voice's registers 0 (volumes, pitch, start,
 * ADSR, envelope level, repeat address) and PMON, NON and EON off. The rehearsal of 2026-10-08 (a stand-in console's run
 * graded against references that repeat one test): spu-curvol's first line, every voice's current volume (1E00h), read
 * what the test before had left - 7FFEh after spu-curvol itself, 0000h after dma-rates. The main and reverb volumes, the
 * reverb's settings and the CD's volume are the machine's own and stay as found.
 * And the memory system (the rehearsal of 2026-10-08, every program back to back on the virtual console against each one
 * run fresh): bios-window's ROM reads took 38 cycles where a fresh machine's took 32, fill-gap's the same 18.8% - a test
 * before them (mem-delay sets the delay/size registers) left 1F801010h as it wanted it. So the nine memory-control
 * registers, RAM_SIZE, the cache control word, SPUCNT and the SPU's transfer control go back as the runner found them too.
 */
/*
 * A CLEAN SLATE for what a test can find but not set back (runner v4; the full rehearsal of 2026-10-08, 250 runs of a
 * stand-in console graded against references that repeat one test): spu-decode-ahead's 1008h row read 685,824 cycles
 * apart and spu-lead a 210-sample onset where its own other run read 0.3 - each read sound RAM an earlier test had left.
 * A console runs the tests in its own order and the reference repeats one, so every run starts with sound RAM zeroed
 * (512 KB by DMA4 from 1 KB of zeros, the SPU on and muted for the transfer), the GPU's texture cache cleared (GP0(01h);
 * a stale line is a hit the first textured draw would not have had) and the MDEC reset (1F801824h bit 31).
 * (v8, 2026-10-26) And VRAM zeroed, four 512 x 256 fills: a copy's or a sprite's run-to-run spread on the SCPH-7501 was
 * VRAM's contents in part - what the tests before had drawn (gpu-shape-time68c: a 64 x 64 copy's 28.9 cycles -> 3.0 with
 * VRAM cleared first, every shape's mean within 4 cycles of the uncleared runs'; STATUS "The scan-out's run-to-run spread is
 * partly VRAM's contents").
 */
static uint32_t zeroBlock[256];
static void spuZeroRam() {
    for (int i = 0; i < 256; i++) zeroBlock[i] = 0;
    const uint32_t dpcr = DPCR;
    DPCR = dpcr | 0x00080000u;                          /* DMA4 on */
    SPU_XFER = 0x0004;
    SPUCNT = 0x8000;                                    /* on, muted, no transfer */
    for (int i = 0; i < 100000 && (SPUSTAT & 0x0030); i++) {}
    SPU_XADDR = 0;
    SPUCNT = 0x8020;                                    /* DMA write */
    for (int i = 0; i < 100000 && (SPUSTAT & 0x0030) != 0x0020; i++) {}
    for (int n = 0; n < 512; n++) {                     /* 512 x 1 KB */
        MADR(4) = (uint32_t)zeroBlock;
        BCR(4) = (16u << 16) | 16u;                     /* 16 blocks of 16 words */
        CHCR(4) = 0x01000201u;
        for (int i = 0; i < 1000000 && (CHCR(4) & 0x01000000u); i++) {}
    }
    for (int i = 0; i < 100000 && (SPUSTAT & 0x0400); i++) {}
    SPUCNT = 0x8000;
    DPCR = dpcr;
}
static void restoreKernel() {
    for (int n = 0; n < 7; n++) CHCR(n) = 0;
    volatile uint32_t *k = (volatile uint32_t *)0x80000000, *s = (volatile uint32_t *)SNAP_BASE;
    for (uint32_t i = 0; i < 0x4000; i++) k[i] = s[i];
    for (int n = 0; n < 9; n++) MEMCTRL(n) = snapMem[n];   /* first: what follows touches the GPU, SPU and MDEC */
    RAMSIZE = snapRam; CACHECTL = snapCache;
    DPCR = snapDpcr;
    DICR = (snapDicr & 0x00FFFFFFu) | 0x7F000000u;      /* its enables as they were, every flag acknowledged */
    GP1 = 0x00000000;
    GP1 = 0x08000000 | ((snapStat >> 17) & 0x3F) | (((snapStat >> 16) & 1) << 6) | (((snapStat >> 14) & 1) << 7);
    GP1 = 0x04000000 | ((snapStat >> 29) & 3);
    GP1 = 0x03000000 | ((snapStat >> 23) & 1);
    for (uint32_t n = 0; n < 4; n++) {                  /* (v8) VRAM zeroed: 512 x 256 at (0|512, 0|256) */
        for (int i = 0; i < 1000000 && !(GPUSTAT & 0x04000000u); i++) {}
        GP0 = 0x02000000u;
        GP0 = ((n >> 1) * 256u << 16) | ((n & 1) * 512u);
        GP0 = (256u << 16) | 512u;
    }
    for (int i = 0; i < 1000000 && !(GPUSTAT & 0x04000000u); i++) {}
    GP0 = 0x01000000;                                   /* the texture cache cleared */
    MDEC1 = 0x80000000u;                                /* the MDEC reset */
    for (int n = 0; n < 2; n++) { TTARGET(n) = snapTTarget[n]; TMODE(n) = snapTMode[n]; }
    SPU_KOFF_LO = 0xFFFF; SPU_KOFF_HI = 0x00FF;
    for (uint32_t a = 0x1F801C00u; a < 0x1F801D80u; a += 2) *(volatile uint16_t *)a = 0;
    for (uint32_t a = 0x1F801D90u; a < 0x1F801D9Cu; a += 2) *(volatile uint16_t *)a = 0;
    spuZeroRam();
    SPUCNT = snapSpucnt; SPU_XFER = snapXfer;
    I_MASK = snapMask;
    I_STAT = 0;
}

#define LOG_SIZE (256 * 1024)
#define LOG_BASE 0x80140000u
static uint8_t *const logBuf = (uint8_t *)LOG_BASE;   /* not in the image: the 256 KB below it */
static uint32_t logLen;                                /* characters printed; the first LOG_SIZE are kept */
extern "C" { uint32_t runStack[4096] __attribute__((aligned(16))); uint32_t jmpBuf[12]; }   /* the runner's own stack (16 KB in its image) and its way back; plain C names, the asm uses both */
static uint8_t header[2048];
/* The line being printed, apart from the log (which may be full): its first 32 characters and its last 16. */
static char lineHead[32], lineTail[16];
static uint32_t lineN;
static uint8_t endMode, endCount, endSeen, endLen, abortOk;
static char endText[32];
static uint32_t cyclesHi;
static uint16_t lastT2;
static uint32_t savedA3C, savedB3D, savedA3E, savedB3F, *b0Table;
static uint32_t entrySr;
static uint32_t runStart;   /* a static: a local set before runner_setjmp is not what longjmp brings back */

extern "C" {
int  runner_setjmp(uint32_t *buf);
void runner_longjmp(uint32_t *buf, int v);
void runner_call(uint32_t pc, uint32_t sp, uint32_t gp);
uint32_t *runner_b0table(void);
void runner_flush(void);
void runner_main(void);
int  runner_putchar(int c);
uint32_t runner_align(volatile uint16_t *counter, uint32_t target);
uint32_t runner_probe(volatile uint16_t *t2, uint32_t target, volatile uint32_t *ramWord);
uint32_t runner_spuprobe(volatile uint16_t *t2, uint32_t target, volatile uint16_t *spustat);
uint32_t runner_read16(volatile uint16_t *t2, uint32_t target, volatile uint16_t *reg);
uint32_t runner_read32(volatile uint16_t *t2, uint32_t target, volatile uint32_t *reg);
}

/* setjmp / longjmp: s0-s8, sp, gp, ra. */
__asm__(
    ".set push\n.set noreorder\n"
    ".global runner_setjmp\nrunner_setjmp:\n"
    "  sw $s0, 0($a0)\n  sw $s1, 4($a0)\n  sw $s2, 8($a0)\n  sw $s3, 12($a0)\n  sw $s4, 16($a0)\n  sw $s5, 20($a0)\n"
    "  sw $s6, 24($a0)\n  sw $s7, 28($a0)\n  sw $fp, 32($a0)\n  sw $sp, 36($a0)\n  sw $gp, 40($a0)\n  sw $ra, 44($a0)\n"
    "  jr $ra\n  move $v0, $zero\n"
    ".global runner_longjmp\nrunner_longjmp:\n"
    /* ra first: a load's register is not there for the very next opcode (the R3000's load delay slot) - with it last,
     * `jr $ra` took the OLD ra, and the virtual console showed it (2026-10-08) */
    "  lw $ra, 44($a0)\n  lw $s0, 0($a0)\n  lw $s1, 4($a0)\n  lw $s2, 8($a0)\n  lw $s3, 12($a0)\n  lw $s4, 16($a0)\n"
    "  lw $s5, 20($a0)\n  lw $s6, 24($a0)\n  lw $s7, 28($a0)\n  lw $fp, 32($a0)\n  lw $sp, 36($a0)\n  lw $gp, 40($a0)\n"
    "  jr $ra\n  move $v0, $a1\n"
    /* the program, called with its own stack and GP; if main returns, the runner takes it as the end */
    ".global runner_call\nrunner_call:\n"
    "  move $sp, $a1\n  move $gp, $a2\n  jalr $a0\n  nop\n"
    "  la $a0, jmpBuf\n  j runner_longjmp\n  li $a1, 2\n"
    /* B(57h) GetB0Table */
    ".global runner_b0table\nrunner_b0table:\n"
    "  li $t1, 0x57\n  j 0xB0\n  nop\n"
    /* A(44h) FlushCache */
    ".global runner_flush\nrunner_flush:\n"
    "  li $t1, 0x44\n  j 0xA0\n  nop\n"
    /* runner_align(counter, target): back once the 16-bit counter (a timer at the system clock) has reached `target`,
     * to the cycle - a spin to within 240 counts, one more read, and a slide down the last part of 256 nops (cached code:
     * a nop a cycle). Registers t0-t3 only, no memory: what it costs is the same every time. ALIGN_OVH (16) is what the
     * read, the arithmetic and the jump cost before the slide; a target under 256 + 16 counts ahead is reached late. */
    ".global runner_align\nrunner_align:\n"
    "1:\n  lhu $t0, 0($a0)\n  nop\n  subu $t1, $a1, $t0\n  andi $t1, $t1, 0xFFFF\n  sltiu $t2, $t1, 240\n  beqz $t2, 1b\n  nop\n"
    "  lhu $t0, 0($a0)\n  nop\n  subu $t1, $a1, $t0\n  andi $t1, $t1, 0xFFFF\n  addiu $t1, $t1, -16\n"
    "  bltz $t1, 3f\n  nop\n"
    "  la $t3, 2f\n  sll $t1, $t1, 2\n  subu $t3, $t3, $t1\n  jr $t3\n  nop\n"
    "  .rept 256\n  nop\n  .endr\n"
    "2:\n3:\n  jr $ra\n  nop\n"
    /* runner_probe(T2, target, ramWord): Timer 2 aligned to `target`, then one RAM load between two reads of Timer 2 -
     * their difference, longer by the stall when a DRAM refresh is on the bus. No stack: ra kept in t9. */
    ".global runner_probe\nrunner_probe:\n"
    "  move $t9, $ra\n  jal runner_align\n  nop\n"
    "  lhu $t4, 0($a0)\n  lw $t5, 0($a2)\n  lhu $t6, 0($a0)\n  nop\n"
    "  subu $v0, $t6, $t4\n  jr $t9\n  andi $v0, $v0, 0xFFFF\n"
    /* runner_spuprobe(T2, target, SPUSTAT): Timer 2 aligned to `target`, then one read of SPUSTAT - its bit 11 */
    ".global runner_spuprobe\nrunner_spuprobe:\n"
    "  move $t9, $ra\n  jal runner_align\n  nop\n"
    "  lhu $t4, 0($a2)\n  nop\n  srl $v0, $t4, 11\n  jr $t9\n  andi $v0, $v0, 1\n"
    /* runner_read16(T2, target, reg): Timer 2 aligned to `target`, then one 16-bit read - ONE copy of the read after the
     * slide, so its instructions are the same cache lines every time (the vernier's first version read through three
     * copies the compiler made of its loop, and each copy's first read came 5 cycles late from a cold line) */
    ".global runner_read16\nrunner_read16:\n"
    "  move $t9, $ra\n  jal runner_align\n  nop\n"
    "  lhu $v0, 0($a2)\n  jr $t9\n  nop\n"
    /* runner_read32: the same with one 32-bit read (v7: GPUSTAT, for bit 31) */
    ".global runner_read32\nrunner_read32:\n"
    "  move $t9, $ra\n  jal runner_align\n  nop\n"
    "  lw $v0, 0($a2)\n  jr $t9\n  nop\n"
    /* the entry: our own stack first, then C */
    ".global _start\n_start:\n"
    "  la $sp, runStack + 16384 - 16\n  la $gp, _gp\n  j runner_main\n  nop\n"
    ".set pop\n");

static inline uint32_t getSr() { uint32_t v; __asm__ volatile("mfc0 %0, $12" : "=r"(v)); return v; }
static inline void setSr(uint32_t v) { __asm__ volatile("mtc0 %0, $12\n nop\n nop" :: "r"(v)); }

static void put(uint8_t b) { while (!(SIO1_STAT & 1)) {} SIO1_DATA = b; }
static void putW(uint32_t w) { put(w); put(w >> 8); put(w >> 16); put(w >> 24); }
static void putS(const char *s) { while (*s) put((uint8_t)*s++); }
static uint8_t get() { while (!(SIO1_STAT & 2)) {} return SIO1_DATA; }
static uint32_t getW() { uint32_t v = get(); v |= get() << 8; v |= (uint32_t)get() << 16; v |= (uint32_t)get() << 24; return v; }

/* Timer 2 at the system clock, its wraps counted whenever it is read: a run's length in cycles. */
/* BRANCHLESS (runner v2): it runs inside every putchar, and a branch on whether Timer 2 wrapped made the test's printf a
 * cycle longer or shorter by Timer 2's value - so each locked run's next measurement started at another phase of the
 * refresh and the GPU (the virtual console, 2026-10-08: dma/chopping's rows varied with Timer 2's value at the start). */
static uint32_t cycles() { const uint16_t t = T2_COUNT; cyclesHi += (uint32_t)(t < lastT2); lastT2 = t; return (cyclesHi << 16) | t; }

/* The line just ended: its own end? "Done." or "Done", alone or after "name: "; or the count-th that starts with endText. */
static bool endLine() {
    if (endMode == 1) {
        if (lineN < endLen) return false;
        for (uint32_t i = 0; i < endLen; i++) if (lineHead[i] != endText[i]) return false;
        return ++endSeen >= endCount;
    }
    /* the line's end, trailing blanks dropped, from the last 16 characters kept */
    const uint32_t lo = lineN > 16 ? lineN - 16 : 0;
    uint32_t n = lineN;
    while (n > lo && (lineTail[(n - 1) & 15] == '\r' || lineTail[(n - 1) & 15] == ' ')) n--;
    const auto c = [n](uint32_t k) { return lineTail[(n - k) & 15]; };   /* the k-th character from the end */
    if (n - lo >= 5 && c(5) == 'D' && c(4) == 'o' && c(3) == 'n' && c(2) == 'e' && c(1) == '.')
        return n == 5 || (n - lo >= 7 && c(6) == ' ' && c(7) == ':');
    return n == 4 && c(4) == 'D' && c(3) == 'o' && c(2) == 'n' && c(1) == 'e';
}

int runner_putchar(int c) {
    cycles();
    if (logLen < LOG_SIZE) logBuf[logLen] = (uint8_t)c;
    logLen++;
    if (c == '\n') {
        const bool done = endLine();
        lineN = 0;
        if (done) { setSr(getSr() & ~1u); runner_longjmp(jmpBuf, 1); }
    } else {
        if (lineN < 32) lineHead[lineN] = (char)c;
        lineTail[lineN & 15] = (char)c;
        lineN++;
    }
    if (abortOk && (SIO1_STAT & 2) && SIO1_DATA == 'X') { setSr(getSr() & ~1u); runner_longjmp(jmpBuf, 3); }
    return c;
}

/*
 * The kernel's puts, A(3Eh) / B(3Fh), too: psx-spx "Like printf, but doesn't resolve any % operands", nothing appended,
 * "<NULL>" for a null pointer. ps1-tests' gpu/bandwidth prints through it and nothing else - under the first runner it
 * printed nothing and never ended (the virtual console, 2026-10-08); cputest.js has always read both vectors.
 */
extern "C" int runner_puts(const char *s) {
    if (!s) s = "<NULL>";
    while (*s) runner_putchar((uint8_t)*s++);
    return 0;
}

static void hook() {
    volatile uint32_t *a = (volatile uint32_t *)0x80000200;
    b0Table = runner_b0table();
    savedA3C = a[0x3C]; savedB3D = b0Table[0x3D]; savedA3E = a[0x3E]; savedB3F = b0Table[0x3F];
    a[0x3C] = (uint32_t)runner_putchar;
    b0Table[0x3D] = (uint32_t)runner_putchar;
    a[0x3E] = (uint32_t)runner_puts;
    b0Table[0x3F] = (uint32_t)runner_puts;
}
static void unhook() {
    volatile uint32_t *a = (volatile uint32_t *)0x80000200;
    a[0x3C] = savedA3C; b0Table[0x3D] = savedB3D; a[0x3E] = savedA3E; b0Table[0x3F] = savedB3F;
}

static void sioInit() {
    SIO1_CTRL = 0x0040;                  /* reset */
    SIO1_MODE = 0x004E;                  /* 8N1, x16 */
    SIO1_BAUD = 18;                      /* 115,200 (Unirom's: 33,868,800 / (16 x 18) = 117,600) */
    SIO1_CTRL = 0x0027;                  /* TX, RX, DTR, RTS */
}

static void load() {
    const uint32_t size = getW();
    uint32_t sum = 0;
    for (int i = 0; i < 2048; i++) { header[i] = get(); sum += header[i]; }
    const uint32_t tAddr = *(uint32_t *)(header + 0x18);
    /* a program that would land on the kernel's copy, the log or the runner: its bytes still taken off the cable, then refused */
    const bool big = (tAddr & 0x1FFFFFFFu) + (size - 2048) > (SNAP_BASE & 0x1FFFFFFFu);
    uint8_t *dst = (uint8_t *)tAddr;
    for (uint32_t i = 2048; i < size; i++) { const uint8_t b = get(); sum += b; if (!big) dst[i - 2048] = b; }
    const uint32_t want = getW();
    putS(big ? "BIG!" : want == sum ? "OKAY" : "BAD!");
}

/*
 * THE DRAM REFRESH, FOUND (2026-10-08, runner v2). psx-spx (live: memorycontrol.md, DRAM_CTRL at 1F801060h - "Refresh
 * period (0..3=(256 + 64*N) cycles)", logic-analyser waveforms; 0B88h, the BIOS's value, is 256) and Sony's bus capture
 * (TUTO3.PAD: 2,219 of 4,420 refreshes exactly 256 clocks apart, the rest waiting for the access on the bus) put a
 * refresh on the main-RAM bus every 256 CPU cycles, free-running since power-on: the same 256-cycle grid as Timer 2 at
 * the system clock, at an offset nobody knows. So one RAM load is timed between two reads of Timer 2 with the load at
 * each of the 256 phases of Timer 2 (runner_probe, a cycle-exact slide before it): where it meets the refresh it takes
 * up to five cycles longer. refreshTable keeps each phase's time (sent by 'F', a console's own measurement), refreshR0
 * the first phase of the longest - the grid a locked run starts on. Probed again in every locked run: the run's write to
 * Timer 2's mode restarts the counter.
 */
static uint8_t refreshTable[256];
static uint32_t refreshR0, lockFrames, lockPhase, lockSpu;
/*
 * THE VIDEO WITHIN A CYCLE (runner v4). The lock lands on Timer 1 = V after its reset at a line's start - a CPU cycle,
 * the line's start rounded up - so the line's place within that cycle is left free: a frame is 353,825,472/625 cycles,
 * and under the refresh's lock about 10 of the 625 fractions can come, under the SPU's 3-4. dma/chopping and ps1-tests'
 * timers read a different log for each. So after the landing Timer 1 counts hblanks and is read once a line, 120 lines,
 * line k at floor(((11 + k) * L + K) / 625) + d_k cycles on (L = 1,345,344 625ths of a cycle a line, K this core's tick
 * offset, measured): it has line k's tick in its count if and only if phi <= d_k - frac, phi being the tick's place
 * against the landing's cycles. The first line, read 100 cycles early, gives the count's base (a tick right after the
 * mode write, or the one t1Swallow takes, make it 0-2); seven lines binary-search d over -64..+64 (a console's tick may
 * show cycles off this core's); the rest read at d - 1, d, d + 1, and the lines' fractions spread their thresholds over
 * the cycle: phi to 0.02-0.05 of a cycle. Each read is ONE routine's (runner_read16) - the loop's copies each came 5
 * cycles late on their first, cold, read. MEASURED on the virtual console (2026-10-08): six classes 0.07-0.12 apart, the
 * same phi for the same fraction, and dma/chopping one log a phi, whether the frame came free or was chosen ('S'/'U' with
 * phi: frames are passed over until phi matches, modulo a cycle). The test starts at a fixed point after the last read.
 */
#define VERN_N 120
#define VERN_K 1846496u   /* 2,954.39 cycles from a line's start to its tick, as this core shows it (scratch_vtick) */
/* (v6) in 1250ths of a cycle: VERN_K twice, and the line the host says (vernLine; 0 = this core's two-crystal 2,152.5504
 * = 2,690,688 / 1,250). A console's line differs from that by its board: the 7501's and 9001's is 2,152.5 exactly (one
 * oscillator, 65/41 - runner v5's reads, 2026-10-08), and over 120 lines 2 ppm is half a cycle, so v5 read "phi unread"
 * on every console. */
#define VERN_K2 3692992u
static uint32_t vernLine = 2690688u;
static uint16_t vernS[VERN_N];
static uint8_t vernC[VERN_N];
static int16_t vernD[VERN_N];
static uint32_t landT2;
static void vernPlan(uint32_t V) {
    for (int k = 0; k < VERN_N; k++) vernS[k] = (uint16_t)((int)((((uint32_t)k + 11u) * vernLine + VERN_K2) / 1250u) - (int)V);
}
/* The first line reads 600 cycles before this core's tick: the count's base (a tick just after the mode write, or one the
 * lock's reset left to be swallowed - src/psx.js t1Swallow - make it 0, 1 or 2). Ten lines then find the tick's offset to
 * a cycle (a binary search over -512..+512), the rest read at it -1, 0 and +1. v4 searched -64..+64 from a base read 100
 * early, and the SCPH-7501 (2026-10-08) saw its tick on every line at -64: its tick comes 64-100 cycles before this
 * core's (runner v5). */
static void vernier() {
    TMODE(1) = 0x0100;           /* hblanks, free running: the write starts it at 0 */
    int lo = -512, hi = 512, d = -600;
    uint32_t base = 0;
    for (int k = 0; k < VERN_N; k++) {
        d = k == 0 ? -600 : k < 11 ? (lo + hi) >> 1 : hi - 1 + (k - 11) % 3;
        const uint32_t c = runner_read16(&T2_COUNT, (landT2 + vernS[k] + (uint32_t)d) & 0xFFFF, &T1_COUNT);   /* line 0 warms it */
        vernC[k] = (uint8_t)c; vernD[k] = (int16_t)d;
        if (k == 0) base = c;
        else if (k < 11) { if (c - base > (uint32_t)k) hi = d; else lo = d; }
    }
}
/* phi, the tick's place against the landing's cycles, in 625ths of a cycle: read k (line 11 + k, offset d_k) saw its tick
 * if and only if phi <= d_k - frac(x_k), x_k = ((11 + k) * 1345344 + VERN_K) / 625 - so each read bounds it, from below or
 * above, and vernLo < phi <= vernHi. (The virtual console, 2026-10-08: 0.02-0.05 of a cycle wide, the same for the same
 * fraction of the frame and apart for others - six classes 0.09 cycle apart or more.) */
/*
 * (v7) GPUSTAT BIT 31's TOGGLE, read the same way. The SCPH-5501 (2026-10-08, console-logs/2026-10-08-SCPH-5501-x2sweep)
 * puts Timer 1's tick a fixed ~23 GPU ticks after the display's end - psx-spx's width, (((X2-X1)/dots)+2) AND NOT 3
 * pixels, with the division rounded up - which at 260h..C60h is ~221 ticks before this core's line end; gpustat31 (console
 * day, three consoles) read bit 31 against Timer 1's parity as ~198. Both are the tick against a reference: here the
 * landing, there the toggle. So with hrange bit 30 the toggle is read too, after the tick's lines, and the two land in
 * one frame: tick minus toggle to a fraction of a cycle. TOG_N lines from TOG_SKIP after the landing (V = 24,000: the
 * landing near line 29, the tick's lines 40-159, these 164-243 - bit 31 reads 0 in v-blank, from line 256); each line
 * read TOG_K after this core's tick (200 GPU ticks, src/psx.js T1_RETRACE: 126.16 cycles), the first 600 early for the
 * parity, ten to find it to a cycle, the rest at -1, 0, +1.
 */
#define TOG_N 80
#define TOG_SKIP 135u
#define TOG_K 157700u
static uint16_t togS[TOG_N];
static uint8_t togC[TOG_N];
static int16_t togD[TOG_N];
static bool togOn;
static void togPlan(uint32_t V) {
    for (int k = 0; k < TOG_N; k++) togS[k] = (uint16_t)((int)((((uint32_t)k + TOG_SKIP) * vernLine + VERN_K2 + TOG_K) / 1250u) - (int)V);
}
static void togVernier() {
    int lo = -512, hi = 512, d = -600;
    uint32_t base = 0;
    for (int k = 0; k < TOG_N; k++) {
        d = k == 0 ? -600 : k < 11 ? (lo + hi) >> 1 : hi - 1 + (k - 11) % 3;
        const uint32_t b = runner_read32(&T2_COUNT, (landT2 + togS[k] + (uint32_t)d) & 0xFFFF, &GPUSTAT) >> 31;
        togC[k] = (uint8_t)b; togD[k] = (int16_t)d;
        if (k == 0) base = b;
        else if (k < 11) { if ((b ^ base ^ ((uint32_t)k & 1u)) != 0) hi = d; else lo = d; }   /* line k's toggle seen */
    }
}
static int32_t vernLo, vernHi;
static void vernDecode() {
    int32_t lo = -0x7FFFFFFF, hi = 0x7FFFFFFF;
    const uint32_t base = vernC[0];
    for (int k = 1; k < VERN_N; k++) {
        const uint32_t num = ((uint32_t)k + 11u) * vernLine + VERN_K2;
        const int32_t t = (int32_t)vernD[k] * 1250 - (int32_t)(num % 1250u);   /* (v6) phi in 1250ths */
        const int32_t b = (int32_t)vernC[k] - (int32_t)base - k;
        if (b == 1) { if (t < hi) hi = t; } else if (b == 0) { if (t > lo) lo = t; }
    }
    vernLo = lo; vernHi = hi;
}
static volatile uint32_t probeWord;
static void probeRefresh() {
    for (int pass = 0; pass < 2; pass++)                 /* the first pass puts the probe and its slide in the I-cache */
        for (uint32_t p = 0; p < 256; p++) {
            const uint32_t tgt = (((uint32_t)T2_COUNT + 600) & 0xFF00) | p;
            const uint32_t d = runner_probe(&T2_COUNT, tgt & 0xFFFF, &probeWord);
            if (pass) refreshTable[p] = d > 255 ? 255 : (uint8_t)d;
        }
    uint32_t best = 0;
    for (uint32_t p = 1; p < 256; p++) if (refreshTable[p] > refreshTable[best]) best = p;
    while (refreshTable[(best + 255) & 255] == refreshTable[best] && best != ((best + 1) & 255)) {   /* the run's first phase */
        best = (best + 255) & 255;
        if (best == 0 && refreshTable[255] == refreshTable[0]) break;
    }
    refreshR0 = best;
}

/*
 * THE SPU's SAMPLE CLOCK, FOUND (runner v3). The SPU makes a sample every 768 CPU cycles off the same crystal, so its
 * phase against the CPU is as fixed as the refresh's - and a locked start that fixes the refresh's 256 still leaves it
 * one of three 256-cycle thirds of a sample, chosen by which frame happened to match (the virtual console, 2026-10-08:
 * spu-irq read 8 lines differently from run to run under 'S'). SPUSTAT bit 11 is the capture buffers' half (psx-spx: 0
 * first, 1 second - 256 samples each), so it changes every 196,608 cycles, which is 3 x 65,536: every change comes at
 * the same Timer 2 value. The probe polls for one, then reads bit 11 once at Timer 2 = that value + k on each change after
 * it (runner_spuprobe, the same slide as the refresh's) and binary-searches k to the cycle; spuS0 is where the change
 * shows, on the extended count (cycles(), whose wraps must be counted all the way to the lock - every wait in between
 * calls it), mod 1,536 - TWO samples: the reverb works at 22,050 Hz, left on one sample and right on the next, and with
 * the start locked to one sample's phase spu-irq's reverb row still moved by 768 cycles from run to run (the virtual
 * console, 2026-10-08). A change of bit 11 comes every 256 samples, an even number, so the reverb's half at it is the
 * same every time if one counter drives both (this core's capturePos and reverbResamplePos both start at power-on and
 * step every sample; a console that disagrees will show it as two logs at one phase). psx-spx: bit 11 reads only with
 * 1F801DACh bit 2 or 3 set, so the probe sets bit 2 if neither is (and puts it back). No change in 450,000 cycles, or a
 * search that does not hold up when checked again: FFFFFFFFh.
 */
static uint32_t spuS0 = 0xFFFFFFFFu, spuNext;
static uint32_t spuBit() { return (SPUSTAT >> 11) & 1; }
static bool spuTrial(int k) {
    while ((int32_t)(cycles() - (spuNext - 3000)) < 0) {}
    const uint32_t before = spuBit();
    const uint32_t b = runner_spuprobe(&T2_COUNT, (spuNext + (uint32_t)k) & 0xFFFF, &SPUSTAT);
    spuNext += 196608;
    return b != before;
}
static uint32_t runnerFlags;   /* (v4) bit 0: the SPU was off and the probe switched it on (muted) to find its phase */
static bool probeSpuOnce() {
    const uint32_t c0 = cycles(), b0 = spuBit();
    for (;;) {
        const uint32_t b = spuBit(), c = cycles();
        if (b != b0) { spuNext = c + 196608; break; }
        if (c - c0 > 450000u) return false;
    }
    const uint32_t ce = spuNext - 196608;
    int lo = -128, hi = 0;
    if (spuTrial(lo) || !spuTrial(hi)) return false;
    while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (spuTrial(mid)) hi = mid; else lo = mid; }
    if (spuTrial(lo) || !spuTrial(hi)) return false;
    spuS0 = (ce + (uint32_t)hi) % 1536u;
    return true;
}
/* Unirom leaves the SPU switched off (SPUCNT 0000h, the virtual console's snapshot). This core runs the sample clock
 * and the capture position with it off; whether a console does is not known - if bit 11 stands still, the probe switches
 * the SPU on, muted (SPUCNT 8000h), tries again, says so in runnerFlags, and puts SPUCNT back. */
static void probeSpu() {
    spuS0 = 0xFFFFFFFFu;
    runnerFlags &= ~1u;
    const uint16_t xfer = SPU_XFER, cnt = SPUCNT;
    if (!(xfer & 0x000C)) SPU_XFER = (uint16_t)((xfer & ~0x000Eu) | 0x0004);
    if (!probeSpuOnce() && !(cnt & 0x8000)) {
        SPUCNT = 0x8000;
        runnerFlags |= 1u;
        if (!probeSpuOnce()) spuS0 = 0xFFFFFFFFu;
        SPUCNT = cnt;
    }
    SPU_XFER = xfer;
}
/* x mod 1,536 in a fixed number of cycles (the lock's decision must take the same time whatever it decides on, and the
 * R3000's divide is fixed but its multiply is not): x>>9 mod 3 by summing base-4 digits (4, 16, 256, 65536 = 1 mod 3). */
static inline uint32_t mod1536(uint32_t x) {
    uint32_t y = x >> 9;
    y = (y >> 16) + (y & 0xFFFF);
    y = (y >> 8) + (y & 0xFF);
    y = (y >> 4) + (y & 0xF);
    y = (y >> 2) + (y & 3);
    y = (y >> 2) + (y & 3);
    y = (y >> 2) + (y & 3);
    const uint32_t t = (y + 1) >> 2;
    y -= (t << 1) + t;
    return (y << 9) | (x & 511);
}

/*
 * THE LOCKED START ('S', runner v2). 'R' starts a test `delay` cycles after a polled VBlank edge - the poll's own loop
 * takes the edge anywhere in ~8 cycles, and the refresh's phase is the frame's. 'S' starts it to the cycle on both:
 *   THE VIDEO: Timer 1 at the system clock with sync mode 1, "reset counter to 0 at VBlank" (psx-spx) - where its v-blank
 *   gate ENDS, the three consoles' timer-sync rows say (src/psx.js T1_VBLANK, M236: the gate is the IRQ's a line later and
 *   a line longer). So at any point of a frame Timer 1 holds the cycles since the last frame's reset, mod 65,536: 20,000
 *   cycles after the polled IRQ edge runner_align slides to the first Timer 1 = V, a fixed point of the frame. Not in the
 *   first frame after the mode write, whose count runs from the write: that frame is never taken (the virtual console's
 *   landings, 2026-10-08: the first one 944 cycles off the lattice the rest lie on exactly).
 *   THE REFRESH: there, (Timer 2 - refreshR0) mod 256 is where the refresh stands. A frame is 566,120.76 cycles (NTSC:
 *   263 lines of 3412.5 video ticks), 104.76 mod 256, so frame by frame it walks every phase; the run waits for the frame
 *   whose phase is the host's target (within `tol`), at most 1,024 frames.
 *   THE SPU ('U', runner v3): the same with the SPU's phase over two samples, (cycles - spuS0) mod 1,536, as the target
 *   (0-1535) - which fixes the refresh's too, since 1,536 is six of its periods. At most 8,192 frames (a target is one
 *   phase in 1,536: ~26 s on average). No SPU phase found (spuS0 FFFFFFFFh): 'S' with the target mod 256.
 * Then Timer 1 gets its mode back (the write restarts it, at the same cycle every time) and the test starts. 'S' and 'U'
 * answer as 'R' does and then the frames waited, the refresh phase and the SPU phase the test started at, spuS0 and
 * refreshR0 (the host's (spuS0 - refreshR0) mod 256 is how this console's refresh sits against its SPU: what a reference
 * run's PSX.refreshPhase has to match).
 */
static void run(bool locked, bool spu) {
    const uint32_t delay = getW();
    uint32_t target = locked ? getW() : 0xFFFFFFFFu;
    const uint8_t tol = locked ? get() : 0;
    /* (v4) the frame's place within a CPU cycle the host wants, as the vernier reads it (phi in 625ths of a cycle), and
     * how near: 7FFFFFFFh for any - a console never asks (it would wait ten times as long); the virtual console asks for
     * the console's, so that its reference run starts in the same fraction */
    const int32_t phiTarget = locked ? (int32_t)getW() : 0x7FFFFFFF;
    const int32_t phiTol = locked ? (int32_t)getW() : 0;
    /* (v6) the line, in 1250ths of a cycle (0: the two-crystal one), and a GP1(06h) range written before the landing (bit 31
     * set: X1 in bits 0-11, X2 in 12-23) - the restore's GP1(00h) leaves the reset's 200h..C00h, and Timer 1's tick
     * follows X2 (hwtests/t1-hsweep on the SCPH-5501, 2026-10-08), so the vernier at chosen ranges reads the tick's place
     * after X2 to a tenth of a cycle */
    const uint32_t lineArg = locked ? getW() : 0, hrange = locked ? getW() : 0;
    vernLine = lineArg ? lineArg : 2690688u;
    /* (v7) hrange bit 30: the toggle's vernier too; bits 24-26: a display width before the landing (1-5: 256, 320, 512, 640,
     * 368 - GP1(08h)'s horizontal resolution, the rest of the mode as GPUSTAT shows it), so the width's rounding can be read
     * at more than Unirom's 512 */
    togOn = (hrange >> 30) & 1;
    const uint32_t width = (hrange >> 24) & 7;
    const uint8_t mode = get();
    endMode = mode & 0x7F; abortOk = mode >> 7; endSeen = 0; endCount = 1; endLen = 0;
    if (endMode == 1) {
        const uint32_t n = get();
        for (uint32_t i = 0; i < n; i++) { const uint8_t b = get(); if (i < 32) endText[i] = (char)b; }
        endLen = n < 32 ? n : 32;
        endCount = get();
    }
    const uint32_t pc = *(uint32_t *)(header + 0x10), gp = *(uint32_t *)(header + 0x14);
    const uint32_t bAddr = *(uint32_t *)(header + 0x28), bSize = *(uint32_t *)(header + 0x2C);
    const uint32_t sAddr = *(uint32_t *)(header + 0x30), sSize = *(uint32_t *)(header + 0x34);
    for (uint32_t i = 0; i < bSize; i++) ((uint8_t *)bAddr)[i] = 0;
    const uint32_t sp = sAddr ? sAddr + sSize : 0x801FFFF0u;
    logLen = 0; lineN = 0; cyclesHi = 0;
    while (SIO1_STAT & 2) (void)SIO1_DATA;     /* nothing old taken for a stop */
    T2_MODE = 0;                         /* the system clock, free running */
    lastT2 = T2_COUNT;
    restoreKernel();
    if (hrange & 0x80000000u) GP1 = 0x06000000u | (hrange & 0xFFFFFFu);
    if (width >= 1 && width <= 5) {
        const uint32_t st = GPUSTAT;
        uint32_t m = ((st >> 17) & 3) | (((st >> 19) & 1) << 2) | (((st >> 20) & 1) << 3) | (((st >> 21) & 1) << 4) |
                     (((st >> 22) & 1) << 5) | (((st >> 16) & 1) << 6) | (((st >> 14) & 1) << 7);
        m &= ~0x43u;
        m |= width == 5 ? 0x40u : width - 1;
        GP1 = 0x08000000u | m;
    }
    hook();
    runner_flush();
    setSr(getSr() & ~1u);                /* FlushCache leaves through ExitCriticalSection: interrupts off again */
    if (locked) {
        probeRefresh();                  /* Timer 2 restarted above: find the grid again (and warm the slide) */
        probeSpu();                      /* and the SPU's; from here to the lock every wait counts Timer 2's wraps */
        if (spu && spuS0 == 0xFFFFFFFFu) { spu = false; if (target < 1536) target &= 255; }
    }
    lockFrames = 0; lockPhase = 0xFFFFFFFFu; lockSpu = 0xFFFFFFFFu;
    if (locked) { vernPlan(delay); togPlan(delay); }
    for (int k = 0; k < TOG_N; k++) { togC[k] = 0xFF; togD[k] = 0; }
    for (int k = 0; k < VERN_N; k++) { vernC[k] = 0xFF; vernD[k] = 0; }
    vernLo = vernHi = 0;
    const uint32_t maxFrames = phiTarget != 0x7FFFFFFF ? 32768u : spu ? 8192u : 1024u, period = spu ? 1536 : 256;
    const int r = runner_setjmp(jmpBuf);
    if (r == 0) {
        if (!locked) {
            /* a VBlank edge, then `delay` cycles: every test starts at the same point of the video frame */
            I_STAT = ~1u;
            while (!(I_STAT & 1)) {}
            const uint16_t t0 = T2_COUNT;
            while ((uint16_t)(T2_COUNT - t0) < delay) {}
        } else {
            T1_MODE = 0x0003;            /* the system clock, sync mode 1: reset to 0 at VBlank */
            uint32_t sinceWrite = 0;     /* landings since that write: the first counts from the write, not a reset */
            for (;;) {
                I_STAT = ~1u;
                /* no VBlank in 2,000,000 cycles (3.5 frames): a plain start, phase FFFFFFFDh, never a console waiting for
                 * ever (the full rehearsal of 2026-10-08 lost fifo-burst's second pass to a runner that went deaf, cause
                 * not reproduced) */
                { const uint32_t w0 = cycles(); while (!(I_STAT & 1) && cycles() - w0 < 2000000u) {} }
                if (!(I_STAT & 1)) { lockPhase = 0xFFFFFFFDu; break; }
                const uint16_t te = T2_COUNT;
                while ((uint16_t)(T2_COUNT - te) < 20000) cycles();
                /* Timer 1 must be counting here, or runner_align would wait for ever and the console need a reset: if it
                 * stands still, a plain start, and the answer says so (phase FFFFFFFEh) */
                const uint16_t c1 = T1_COUNT;
                { const uint16_t tw = T2_COUNT; while ((uint16_t)(T2_COUNT - tw) < 64) {} }
                if (T1_COUNT == c1) { lockPhase = 0xFFFFFFFEu; break; }
                runner_align(&T1_COUNT, delay);
                /* the same instructions whatever they find, both phases worked out every time */
                const uint32_t C = cycles();
                const uint32_t ph = (C - refreshR0) & 255, ps = mod1536(C - spuS0);
                const uint32_t at = spu ? ps : ph;
                uint32_t dd = at + period - target;
                dd -= period & (0u - (uint32_t)(dd >= period));
                lockFrames++; sinceWrite++;
                const bool capped = lockFrames >= maxFrames;
                if ((sinceWrite > 1 && (target >= period || dd <= tol || period - dd <= tol)) || capped) {
                    lockPhase = ph;
                    lockSpu = spuS0 == 0xFFFFFFFFu ? 0xFFFFFFFFu : ps;
                    landT2 = C;
                    vernier();
                    if (togOn) togVernier();
                    vernDecode();
                    const int32_t mid = (vernLo >> 1) + (vernHi >> 1);
                    /* matched modulo a cycle: a console whose tick shows a whole cycle off this core's reads phi one more or
                     * less for the same fraction of the frame, and the fraction is what the test sees */
                    int32_t df = (mid - phiTarget) % 1250;
                    if (df < 0) df += 1250;
                    if (capped || phiTarget == 0x7FFFFFFF || (vernLo < vernHi && (df <= phiTol || 1250 - df <= phiTol))) break;
                    T1_MODE = 0x0003;    /* another fraction: on to the next frame, Timer 1 back to the lock's count */
                    sinceWrite = 0;
                }
            }
            /* the test starts at one point after the landing whatever the vernier and its decoding took */
            if (lockPhase < 0xFFFFFFF0u) runner_align(&T2_COUNT, (landT2 + (togOn ? togS[TOG_N - 1] : vernS[VERN_N - 1]) + 20000u) & 0xFFFF);
            /* Timer 1 back, and Timer 0 restarted beside it (v4): its restore before the lock left its count running
             * through the wait, and counter-bracket's T0->T2 row read -1 against -6 (the full rehearsal, 2026-10-08) */
            TTARGET(0) = snapTTarget[0]; TMODE(0) = snapTMode[0];
            TTARGET(1) = snapTTarget[1]; TMODE(1) = snapTMode[1];
            T2_MODE = 0;                 /* Timer 2 restarted here too: every locked run starts with the same count in it */
        }
        lastT2 = T2_COUNT; cyclesHi = 0;
        runStart = cycles();
        setSr(entrySr);
        runner_call(pc, sp, gp);
    }
    /* back from the program: interrupts off (runner_putchar did), the vectors put back */
    setSr(getSr() & ~1u);
    const uint32_t took = cycles() - runStart;
    unhook();
    sioInit();
    putS("DONE"); putW(logLen); putW(took); put((uint8_t)r);
    if (locked) { putW(lockFrames); putW(lockPhase); putW(lockSpu); putW(spuS0); putW(refreshR0); putW(runnerFlags); putW((uint32_t)vernLo); putW((uint32_t)vernHi); put(VERN_N); for (int k = 0; k < VERN_N; k++) { put(vernC[k]); put((uint8_t)vernD[k]); put((uint8_t)((uint16_t)vernD[k] >> 8)); }
                  if (togOn) { put(TOG_N); for (int k = 0; k < TOG_N; k++) { put(togC[k]); put((uint8_t)togD[k]); put((uint8_t)((uint16_t)togD[k] >> 8)); } } }
}

/* 'F': the last probe's refresh table - its first phase, then 256 times, then their sum; then (v3) the SPU's spuS0. */
static void sendRefresh() {
    put((uint8_t)refreshR0);
    uint32_t sum = 0;
    for (int i = 0; i < 256; i++) { put(refreshTable[i]); sum += refreshTable[i]; }
    putW(sum);
    putW(spuS0);
}

static void sendLog() {
    const uint32_t n = logLen < LOG_SIZE ? logLen : LOG_SIZE;
    putW(n);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) { put(logBuf[i]); sum += logBuf[i]; }
    putW(sum);
}

static void mem() {
    const uint32_t addr = getW(), size = getW();
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) { const uint8_t b = ((volatile uint8_t *)addr)[i]; put(b); sum += b; }
    putW(sum);
}

static const char RUNNER_ID[] = "PSXRUNNER8 " __DATE__ " " __TIME__ "\n";

void runner_main() {
    entrySr = getSr();
    snapshot();
    setSr(entrySr & ~1u);                /* interrupts off while the runner has the CPU */
    /* Unirom's last word to the PC may still be leaving SIO1 (the upload's final acknowledge): let it go first */
    for (int i = 0; i < 1000000 && !(SIO1_STAT & 4); i++) {}
    T2_MODE = 0;
    for (int k = 0; k < 4; k++) { const uint16_t t0 = T2_COUNT; while ((uint16_t)(T2_COUNT - t0) < 60000) {} }
    probeRefresh();                      /* the table 'F' sends before any test has run */
    lastT2 = T2_COUNT;
    probeSpu();
    sioInit();
    putS("PSXRUNNER8\n");
    for (;;) {
        switch (get()) {
            case 'P': putS("PONG"); break;
            /* 'V' (v5): which runner this is. The PC reuses a runner still resident from its last run when this line is in
             * the runner.exe it would send, so a console needs no RESET between runs (the SCPH-7501, 2026-10-08, did). */
            case 'V': putS(RUNNER_ID); break;
            case 'L': load(); break;
            case 'R': run(false, false); break;
            case 'S': run(true, false); break;
            case 'U': run(true, true); break;
            case 'F': sendRefresh(); break;
            case 'G': sendLog(); break;
            case 'M': mem(); break;
            case 'B': { const uint32_t v = getW(); putS("BAUD"); while (!(SIO1_STAT & 4)) {} SIO1_BAUD = (uint16_t)v; break; }
            default: break;
        }
    }
}
